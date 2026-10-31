#!/usr/bin/env python3
"""hostbuild_zzgl.py OUTDIR -- ioquake3 with the AMIX layer and zzgl for the
PC: amix_glimp_zzgl.c, the 060's encoder and core 1's decoder in one process
(zzgl_host.c) on the PC's build of Mesa 18.3 (z3660gl-amix/src/host-osmesa).
Tests the GL marshalling with the real renderer before the board."""
import os, subprocess, sys

H = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(H, "build.py")).read().replace("\nmain()\n", "\n")
g = {"__file__": os.path.join(H, "build.py")}
exec(compile(src, "build.py", "exec"), g)
out = sys.argv[1]
C = g["C"]
ZZ = os.path.expanduser("~/Sources/usr-amix/amix-gate/usr/src/firmware/core1")
ZZGL = os.path.join(ZZ, "zzgl")
MESA_INC = os.path.join(ZZ, "src", "mesa-18.3.6", "include")
OSMESA = os.path.join(ZZ, "src", "host-osmesa", "src", "gallium", "targets", "osmesa")
os.makedirs(out, exist_ok=True)
CF = ["gcc", "-O2", "-g", "-w", "-fcommon", "-fno-strict-aliasing", "-std=gnu99", "-DBOTLIB", "-DAMIX_ZZGL",
      '-DPRODUCT_VERSION="1.36-amixzzgl"', "-DNO_GZIP", "-DUSE_INTERNAL_JPEG", "-DUSE_INTERNAL_ZLIB",
      "-I" + os.path.join(H, "include"), "-I" + os.path.join(C, "thirdparty", "jpeg-9f"),
      "-I" + os.path.join(C, "thirdparty", "zlib-1.3.1"), "-I" + ZZGL, "-I" + g["MESA"] + "/include"]


def cc(flags, p, o):
    if not (os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(p)):
        r = subprocess.run(flags + ["-c", p, "-o", o], capture_output=True, text=True)
        if r.returncode:
            print("FAIL", p, r.stderr[-600:])
            sys.exit(1)
    return o


objs = []
for grp, srcs in g["GROUPS"]:
    for s in srcs:
        b = os.path.basename(s)
        if b in ("amix_compat.c", "amix_glprocs.c", "tr_amix_s3d.c"):
            continue
        if b == "amix_glimp.c":
            s = s.replace("amix_glimp.c", "amix_glimp_zzgl.c")
        p = os.path.normpath(os.path.join(C, s))
        objs.append(cc(CF, p, os.path.join(out, grp + "_" + os.path.basename(s)[:-2] + ".o")))
for a in ("asm/ftola.c", "asm/snapvector.c", "qcommon/vm_x86.c"):
    objs.append(cc(CF, os.path.join(C, a), os.path.join(out, "asm_" + os.path.basename(a)[:-2] + ".o")))
subprocess.run(["python3", "-I", os.path.join(ZZGL, "gen.py"), os.path.join(ZZGL, "gen")], check=True)
ZF = ["gcc", "-O2", "-g", "-Wall", "-Wno-unused-function", "-DZZGL_TRACE", "-I" + ZZGL, "-I" + MESA_INC]
# DIRECT=1: the reference, the GL calls straight into the PC's Mesa (zzgl_direct.c)
direct = os.environ.get("DIRECT") == "1"
# the zzgl objects always fresh: they depend on zzgl's headers
for f in ("zzgl_enc", "zzgl_dec", "zzgl_host", "zzgl_direct"):
    if os.path.exists(os.path.join(out, "zz_" + f + ".o")):
        os.remove(os.path.join(out, "zz_" + f + ".o"))
for f in (("zzgl_direct.c",) if direct else ("zzgl_enc.c", "zzgl_dec.c", "zzgl_host.c")):
    objs.append(cc(ZF, os.path.join(ZZGL, f), os.path.join(out, "zz_" + f[:-2] + ".o")))
r = subprocess.run(["gcc", "-o", os.path.join(out, "ioq3-direct" if direct else "ioq3-zzgl")] + objs +
                   ["-L" + OSMESA, "-lOSMesa", "-Wl,-rpath," + OSMESA, "-lX11", "-lXext", "-lm", "-ldl"],
                   capture_output=True, text=True)
print("engine:", "ok" if r.returncode == 0 else r.stderr[-1500:])
for name, define, lst in g["MODULES"]:
    mo = []
    for s in lst.split():
        p = os.path.join(C, s)
        mo.append(cc(CF + ["-fPIC", "-D" + define], p, os.path.join(out, "m" + name + "_" + os.path.basename(s)[:-2] + ".o")))
    r = subprocess.run(["gcc", "-shared", "-o", os.path.join(out, name + "x86_64.so")] + mo + ["-lm"],
                       capture_output=True, text=True)
    print(name, "ok" if r.returncode == 0 else r.stderr[-400:])
