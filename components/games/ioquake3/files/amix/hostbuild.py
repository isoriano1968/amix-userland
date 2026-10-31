#!/usr/bin/env python3
"""hostbuild.py OUTDIR HOSTMESA_LIBGL -- ioquake3 with the AMIX layer
(amix_glimp.c, amix_input.c) for the PC, against a PC build of mesa31-amix
(libs3d in S3D_FAKE mode), to profile the 68k side of the GL path with
callgrind: the same engine, renderer and Mesa code, x86-64 instructions."""
import os, subprocess, sys, importlib.util
H = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("b", os.path.join(H, "build.py"))
src = open(os.path.join(H, "build.py")).read().replace("\nmain()\n", "\n")
g = {"__file__": os.path.join(H, "build.py")}
exec(compile(src, "build.py", "exec"), g)
out, libgl = sys.argv[1], sys.argv[2]
C = g["C"]; MESA = g["MESA"]
os.makedirs(out, exist_ok=True)
CF = ["gcc", "-O2", "-g", "-w", "-fcommon", "-fno-strict-aliasing", "-std=gnu99", "-DBOTLIB", "-DAMIX_S3D",
      '-DPRODUCT_VERSION="1.36-amixhost"', "-DNO_GZIP", "-DUSE_INTERNAL_JPEG", "-DUSE_INTERNAL_ZLIB",
      "-I" + os.path.join(H, "include"), "-I" + os.path.join(C, "thirdparty", "jpeg-9f"),
      "-I" + os.path.join(C, "thirdparty", "zlib-1.3.1"), "-I" + MESA + "/include"]
objs = []
for grp, srcs in g["GROUPS"]:
    for s in srcs:
        if s.endswith("amix_compat.c"):
            continue
        p = os.path.normpath(os.path.join(C, s))
        o = os.path.join(out, grp + "_" + os.path.basename(s)[:-2] + ".o")
        if not (os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(p)):
            r = subprocess.run(CF + ["-c", p, "-o", o], capture_output=True, text=True)
            if r.returncode:
                print("FAIL", s, r.stderr[-300:]); sys.exit(1)
        objs.append(o)
for a in ("asm/ftola.c", "asm/snapvector.c", "qcommon/vm_x86.c"):
    o = os.path.join(out, "asm_" + os.path.basename(a)[:-2] + ".o")
    subprocess.run(CF + ["-c", os.path.join(C, a), "-o", o], check=True)
    objs.append(o)
r = subprocess.run(["gcc", "-o", os.path.join(out, "ioq3-host")] + objs + [libgl, "-lX11", "-lXext", "-lm", "-ldl"],
                   capture_output=True, text=True)
print("engine:", "ok" if r.returncode == 0 else r.stderr[-800:])
for name, define, lst in g["MODULES"]:
    mo = []
    for s in lst.split():
        p = os.path.join(C, s)
        o = os.path.join(out, "m" + name + "_" + os.path.basename(s)[:-2] + ".o")
        if not (os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(p)):
            r = subprocess.run(CF + ["-fPIC", "-D" + define, "-c", p, "-o", o], capture_output=True, text=True)
            if r.returncode:
                print("FAIL", s, r.stderr[-300:]); sys.exit(1)
        mo.append(o)
    r = subprocess.run(["gcc", "-shared", "-o", os.path.join(out, name + "x86_64.so")] + mo + ["-lm"],
                       capture_output=True, text=True)
    print(name, "ok" if r.returncode == 0 else r.stderr[-400:])
