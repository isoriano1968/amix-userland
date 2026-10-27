#!/usr/bin/env python3
"""build.py -- Blender 2.31a for AMIX (A3000 + Z3660 68060), cross-built with
gcc14-cross-amix.  No Python, no game engine, no sound, no international
fonts: what is left is the modeller, the renderer and the interface.

Each library is built the way the NaN Makefiles build it -- every .c/.cpp of
its directory (or the CCSRCS/CSRCS the Makefile lists), with the include paths
of its `CPPFLAGS +=` lines -- but the Makefiles are only read, never run:
conditionals are evaluated as on Linux, the NAN_* package paths are mapped to
this tree, and the compiler and flags are ours.

    python3 -I build.py [lib ...]     default: every library
    python3 -I build.py --list

Objects and archives go to obj/<lib>/ and lib/; logs to log/.
"""
import os, re, subprocess, sys

H = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(H, "src")
HOME = os.path.expanduser("~")
TC = os.path.join(HOME, "opt/m68k-amix/bin")
CC, CXX, AR = TC + "/m68k-elf-gcc", TC + "/m68k-elf-g++", TC + "/m68k-elf-ar"
XINC = os.path.join(HOME, "opt/m68k-amix-sysroot/usr/x11r6/xc/exports/include")
MESA = os.path.join(HOME, "Sources/usr-amix/mesa31-amix")
DEPS = os.path.join(H, "deps")

COMMON = ["-I" + os.path.join(H, "amixinc"), "-O2", "-m68060", "-funsigned-char", "-U__linux__", "-Ulinux",
          "-D__amix__", "-D__BIG_ENDIAN__", "-DNDEBUG", "-fcommon", "-w",
          "-I" + XINC, "-I" + MESA + "/include", "-I" + DEPS + "/include"]
CFLAGS = COMMON + ["-std=gnu89", "-include", os.path.join(H, "ccompat.h"),
                   "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
                   "-Wno-error=incompatible-pointer-types", "-Wno-error=implicit-int",
                   "-Wno-error=return-mismatch"]
CXXFLAGS = COMMON + ["-std=gnu++98", "-fpermissive", "-include", os.path.join(H, "cxxcompat.h")]

# where each NAN_* package's headers live in this tree
NAN = {
    "NAN_GUARDEDALLOC": "intern/guardedalloc", "NAN_BMFONT": "intern/bmfont",
    "NAN_GHOST": "intern/ghost", "NAN_STRING": "intern/string",
    "NAN_BSP": "intern/bsp/extern", "NAN_DECIMATION": "intern/decimation/extern",
    "NAN_IKSOLVER": "intern/iksolver/extern", "NAN_MOTO": "intern/moto/include",
    "NAN_SOUNDSYSTEM": "intern/SoundSystem", "NAN_MEMUTIL": "intern/memutil",
    "NAN_CONTAINER": "intern/container",
}

# (directory, library); order = link order later
LIBS = [
    ("intern/guardedalloc/intern", "guardedalloc"),
    ("intern/memutil/intern", "memutil"),
    ("intern/container/intern", "container"),
    ("intern/string/intern", "string"),
    ("intern/moto/intern", "moto"),
    ("intern/bmfont/intern", "bmfont"),
    ("intern/ghost/intern", "ghost"),
    ("intern/bsp/intern", "bsp"),
    ("intern/decimation/intern", "decimation"),
    ("intern/iksolver/intern", "iksolver"),
    ("intern/SoundSystem/intern", "SoundSystem"),
    ("intern/SoundSystem/dummy", "DummySoundSystem"),
    ("source/kernel/gen_system", "gen_system"),
    ("source/kernel/gen_messaging/intern", "gen_messaging"),
    ("source/blender/blenlib/intern", "blenlib"),
    ("source/blender/avi/intern", "avi"),
    ("source/blender/imbuf/intern", "imbuf"),
    ("source/blender/img/intern", "img"),
    ("source/blender/blenkernel/intern", "blenkernel"),
    ("source/blender/blenloader/intern", "blenloader"),
    ("source/blender/blenpluginapi/intern", "blenpluginapi"),
    ("source/blender/readblenfile/intern", "readblenfile"),
    ("source/blender/readstreamglue/intern", "readstreamglue"),
    ("source/blender/writestreamglue/intern", "writestreamglue"),
    ("source/blender/writeblenfile/intern", "writeblenfile"),
    ("source/blender/inflate/intern", "inflate"),
    ("source/blender/deflate/intern", "deflate"),
    # the web publisher's RSA/RC4 (OpenSSL) is not built: decrypt's stub only
    ("source/blender/decrypt/stub", "decryptSTUB"),
    ("source/blender/radiosity/intern/source", "radiosity"),
    ("source/blender/render/intern/source", "render"),
    ("source/blender/renderconverter/intern", "renderconverter"),
    ("source/blender/src", "src"),
    ("source/creator", "creator"),
]


def truthy(cond):
    """Evaluate one NaN Makefile conditional as on Linux, without Python/FT2."""
    c = cond.strip()
    m = re.match(r'ifeq\s*\(\$\(OS\),\s*\$\(findstring \$\(OS\),\s*"([^"]*)"\)\)', c)
    if m:
        return "linux" in m.group(1).split()
    m = re.match(r'ifeq\s*\(\$\(OS\),\s*(\w+)\)', c)
    if m:
        return m.group(1) == "linux"
    m = re.match(r'ifneq\s*\(\$\(OS\),\s*(\w+)\)', c)
    if m:
        return m.group(1) != "linux"
    if c.startswith("ifdef") or c.startswith("ifndef"):
        return c.startswith("ifndef")        # nothing optional is defined
    return False                             # other ifeq: CPU, WITH_*, ...


def read_makefile(d):
    """CPPFLAGS (-I/-D) and explicit source lists from directory d's Makefile."""
    mk = os.path.join(SRC, d, "Makefile")
    flags, srcs, stack = [], {}, []
    for line in open(mk, errors="replace"):
        line = line.split("#")[0].rstrip()
        s = line.strip()
        if not s:
            continue
        if re.match(r"(ifeq|ifneq|ifdef|ifndef)\b", s):
            stack.append(truthy(s)); continue
        if s == "else":
            stack[-1] = not stack[-1]; continue
        if s == "endif":
            stack.pop(); continue
        if stack and not all(stack):
            continue
        m = re.match(r"CPPFLAGS\s*\+=\s*(.*)", s)
        if m:
            flags += m.group(1).split()
        m = re.match(r"(CSRCS|CCSRCS)\s*(\+?=|\?=)\s*(.*)", s)
        if m:
            srcs.setdefault(m.group(1), [])
            srcs[m.group(1)] += m.group(3).split()
    return flags, srcs


def expand(flags, d):
    out = []
    for f in flags:
        f = f.replace("$(SRCHOME)", os.path.join(SRC, "source"))
        f = f.replace("$(NANBLENDERHOME)", SRC)
        if "$(OPENGL_HEADERS)" in f or "PYTHON" in f or "$(NAN_SDLCFLAGS)" in f:
            continue
        m = re.match(r"-I\$\((NAN_\w+)\)/include", f)
        if m:
            if m.group(1) in NAN:
                out.append("-I" + os.path.join(SRC, NAN[m.group(1)]))
            elif m.group(1) in ("NAN_JPEG", "NAN_PNG", "NAN_ZLIB"):
                out.append("-I" + DEPS + "/include")
            continue
        if "$(" in f:
            continue                         # an unmapped package: not needed
        if f.startswith("-I") and not f.startswith("-I/"):
            f = "-I" + os.path.normpath(os.path.join(SRC, d, f[2:]))
        out.append(f)
    return out


def sources(d, srcs):
    full = os.path.join(SRC, d)
    if srcs:
        names = []
        import glob
        for k in ("CSRCS", "CCSRCS"):
            text = " ".join(srcs.get(k, []))
            for pat in re.findall(r"\$\(wildcard ([^)]*)\)", text):
                names += [os.path.basename(p) for p in glob.glob(os.path.join(full, pat))]
            text = re.sub(r"\$\(wildcard [^)]*\)", " ", text)
            names += [n for n in text.split() if "$(" not in n]
        if names:
            return sorted(set(names))
    return sorted(n for n in os.listdir(full) if n.endswith((".c", ".cpp")))


def build(d, lib):
    flags, srcs = read_makefile(d)
    inc = expand(flags, d)
    obj = os.path.join(H, "obj", lib)
    os.makedirs(obj, exist_ok=True)
    os.makedirs(os.path.join(H, "lib"), exist_ok=True)
    os.makedirs(os.path.join(H, "log"), exist_ok=True)
    log = open(os.path.join(H, "log", lib + ".log"), "w")
    objs, bad = [], []
    for n in sources(d, srcs):
        cpp = n.endswith(".cpp")
        o = os.path.join(obj, n.rsplit(".", 1)[0] + ".o")
        cmd = [CXX if cpp else CC] + (CXXFLAGS if cpp else CFLAGS) + inc + \
              ["-c", os.path.join(SRC, d, n), "-o", o]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write("$ " + " ".join(cmd) + "\n" + r.stdout)
        (objs if r.returncode == 0 else bad).append(o if r.returncode == 0 else n)
    a = os.path.join(H, "lib", "lib%s.a" % lib)
    if os.path.exists(a):
        os.remove(a)
    if objs:
        subprocess.run([AR, "rcs", a] + objs, check=True)
    print("%-18s %3d ok %3d failed%s" % (lib, len(objs), len(bad),
          (": " + " ".join(bad[:8]) + (" ..." if len(bad) > 8 else "")) if bad else ""))
    return not bad


def main():
    args = sys.argv[1:]
    if args == ["--list"]:
        for d, l in LIBS:
            print(l, d)
        return
    want = set(args)
    ok = True
    for d, l in LIBS:
        if not want or l in want:
            ok &= build(d, l)
    sys.exit(0 if ok else 1)


main()
