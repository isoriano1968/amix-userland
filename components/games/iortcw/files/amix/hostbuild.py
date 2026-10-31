#!/usr/bin/env python3
"""hostbuild.py OUTDIR -- iortcw SP with the AMIX layer and zzgl for the PC: the
060's encoder and core 1's decoder in one process (zzgl_host.c) on the PC's
build of Mesa 18.3 (z3660gl-amix/src/host-osmesa).  Runs the real renderer
where a debugger can reach it."""
import os, subprocess, sys

H = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(H, "build.py")).read().replace("\nmain()\n", "\n")
g = {"__file__": os.path.join(H, "build.py")}
exec(compile(src, "build.py", "exec"), g)
out = sys.argv[1]
C, ZZ = g["C"], g["ZZDIR"]
ZZGL = ZZ + "/zzgl"
MESA_INC = ZZ + "/src/mesa-18.3.6/include"
OSMESA = ZZ + "/src/host-osmesa/src/gallium/targets/osmesa"
os.makedirs(out, exist_ok=True)
CF = ["-O1", "-g", "-w", "-fcommon", "-fno-strict-aliasing", "-DBOTLIB", "-DAMIX_ZZGL",
      '-DPRODUCT_VERSION="1.51-amixhost"', "-DNO_GZIP", "-DUSE_INTERNAL_JPEG", "-DUSE_INTERNAL_ZLIB",
      "-DUSE_FILE32API", "-DNO_VM_COMPILED", "-DAMIX_HOST", '-DARCH_STRING="x86_64"', '-DDLL_EXT=".so"', "-I" + H + "/include", "-I" + C + "/jpeg-8c",
      "-I" + C + "/zlib-1.2.11", "-I" + ZZGL, "-I" + g["MESA"] + "/include"]


def cc(flags, p, o):
    if not (os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(p)):
        tool = ["g++", "-fpermissive", "-fno-exceptions", "-fno-rtti"] if p.endswith(".cpp") else ["gcc", "-std=gnu99"]
        r = subprocess.run(tool + flags + ["-c", p, "-o", o], capture_output=True, text=True)
        if r.returncode:
            print("FAIL", p, r.stderr[-900:])
            sys.exit(1)
    return o


objs = []
for rel, names in g["ENGINE"].items():
    for n in names.split():
        p = g["one"](rel, n)
        objs.append(cc(CF, p, os.path.join(out, rel + "_" + n + ".o")))
for p in g["JPEG"] + g["ZLIB"]:
    objs.append(cc(CF, p, os.path.join(out, "l_" + os.path.basename(p)[:-2] + ".o")))
for n in ("amix_glimp_zzgl.c", "amix_input.c", "amix_snd.c"):
    objs.append(cc(CF, os.path.join(H, n), os.path.join(out, n[:-2] + ".o")))
for a in ("asm/ftola.c", "asm/snapvector.c"):
    objs.append(cc(CF, os.path.join(C, a), os.path.join(out, "asm_" + os.path.basename(a)[:-2] + ".o")))
ZF = ["-O1", "-g", "-Wno-unused-function", "-DZZGL_TRACE", "-I" + ZZGL, "-I" + MESA_INC]
for f in ("zzgl_enc", "zzgl_dec", "zzgl_host"):
    o = os.path.join(out, "zz_" + f + ".o")
    if os.path.exists(o):
        os.remove(o)
    objs.append(cc(ZF, os.path.join(ZZGL, f + ".c"), o))
r = subprocess.run(["g++", "-o", os.path.join(out, "iowolfsp.host")] + objs + ["-L" + OSMESA, "-lOSMesa",
                   "-Wl,-rpath," + OSMESA, "-lX11", "-lXext", "-lm", "-ldl"], capture_output=True, text=True)
print("engine:", "ok" if r.returncode == 0 else "\n".join(sorted(set(l.split("undefined reference to")[-1] for l in r.stderr.splitlines() if "undefined" in l or "multiple" in l))[:30]))
os.makedirs(os.path.join(out, "main"), exist_ok=True)
for name, define, so, names in g["MODULES"]:
    mo = []
    for n in names:
        rel, base = n.rsplit("/", 1)
        for sub in [rel, "game", "cgame", "ui", "qcommon"]:
            p = os.path.join(C, sub, base + ".c")
            if os.path.exists(p):
                break
        mo.append(cc(CF + ["-fPIC"] + ("-D" + define).split(), p, os.path.join(out, "m" + name + "_" + base + ".o")))
    r = subprocess.run(["gcc", "-shared", "-o", os.path.join(out, "main", name + ".sp.x86_64.so")] + mo + ["-lm"],
                       capture_output=True, text=True)
    print(name, "ok" if r.returncode == 0 else r.stderr[-400:])
