#!/usr/bin/env python3
"""build.py -- iortcw (single player) for AMIX, A3000 + Z3660 68060, cross-built
with gcc14-cross-amix: the GL1 renderer linked in, GL on the Z3660's core 1
(zzgl, as ioquake3), X11, no SDL, OpenAL, curl, Ogg or Opus.  The game modules
are native shared objects, as ioquake3's.

    python3 -I amix/build.py [--clean] [modules]     (from iortcw/SP)
"""
import os, re, subprocess, sys

H = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(H)
C = os.path.join(TOP, "code")
HOME = os.path.expanduser("~")
BIN = HOME + "/opt/m68k-amix/bin/"
CC, CXX, LD = BIN + "m68k-elf-gcc", BIN + "m68k-elf-g++", BIN + "m68k-elf-ld"
X = HOME + "/opt/m68k-amix-sysroot/usr/x11r6/xc/exports"
SYSLIB = HOME + "/opt/m68k-amix-sysroot/usr/ccs/lib"
L060 = os.environ.get("LIB060", HOME + "/Sources/usr-amix/amix-userland/components/library/lib060")
MESA = os.environ.get("MESA31", HOME + "/Sources/usr-amix/amix-userland/components/x11/mesa-31/work")
ZZDIR = os.environ.get("Z3660GL", HOME + "/Sources/usr-amix/amix-gate/usr/src/firmware/core1")
H264 = os.environ.get("CORE1_HDR", HOME + "/Sources/usr-amix/amix-gate/usr/src/firmware/core1/cores/h264bench")

CFLAGS = ["-O2", "-m68060", "-fno-strict-aliasing", "-fcommon", "-w", "-U__linux__", "-Ulinux",
          "-D__amix__", "-DSVR4", "-DAMIX_ZZGL", "-DBOTLIB", '-DPRODUCT_VERSION="1.51-amix"',
          "-DNO_GZIP", "-DUSE_INTERNAL_JPEG",
          "-DUSE_INTERNAL_ZLIB", "-DUSE_FILE32API", "-DNO_VM_COMPILED",  "-fno-builtin-sin", "-fno-builtin-cos",
          "-fno-builtin-tan", "-fno-builtin-fmod",
          "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
          "-Wno-error=incompatible-pointer-types", "-Wno-error=implicit-int", "-Wno-error=return-mismatch",
          "-include", os.path.join(H, "include", "amix_compat.h"), "-I" + os.path.join(H, "include"),
          "-I" + os.path.join(C, "jpeg-8c"), "-I" + os.path.join(C, "zlib-1.2.11"),
          "-I" + ZZDIR + "/zzgl", "-I" + MESA + "/include", "-I" + X + "/include"]
ENGINE_FLAGS = ["-DBOTLIB"]
CFLAGS_C = ["-std=gnu99"]
CXXFLAGS = ["-fno-exceptions", "-fno-rtti", "-fpermissive", "-fno-threadsafe-statics"]
ZZFLAGS = ["-I" + ZZDIR + "/src/mesa-18.3.6/include", "-I" + ZZDIR + "/compat/amix", "-I" + H264]


def where():
    m = {}
    for d, _, fs in os.walk(C):
        rel = os.path.relpath(d, C)
        if re.search(r"rend2|SDL2|curl|freetype|opus|ogg|vorbis|tools|/asm|^asm|libs$|null|AL$|^sdl", rel):
            continue
        for f in fs:
            if f.endswith((".c", ".cpp")):
                m.setdefault(rel, []).append(f)
    return m


def one(rel, name):
    for e in (".c", ".cpp"):
        p = os.path.join(C, rel, name + e)
        if os.path.exists(p):
            return p
    raise SystemExit("no " + rel + "/" + name)


ENGINE = {
    "client": "cl_cgame cl_cin cl_console cl_input cl_keys cl_main cl_net_chan cl_parse cl_scrn cl_ui cl_avi "
              "snd_adpcm snd_dma snd_mem snd_mix snd_wavelet snd_main snd_codec snd_codec_wav snd_openal",
    "qcommon": "cm_load cm_patch cm_polylib cm_test cm_trace cmd common cvar files md4 md5 msg net_chan net_ip "
               "huffman q_math q_shared puff vm vm_interpreted",
    "server": "sv_bot sv_ccmds sv_client sv_game sv_init sv_main sv_net_chan sv_snapshot sv_world",
    "botlib": "be_aas_bspq3 be_aas_cluster be_aas_debug be_aas_entity be_aas_file be_aas_main be_aas_move "
              "be_aas_optimize be_aas_reach be_aas_route be_aas_routealt be_aas_routetable be_aas_sample be_ai_char "
              "be_ai_chat be_ai_gen be_ai_goal be_ai_move be_ai_weap be_ai_weight be_ea be_interface l_crc l_libvar "
              "l_log l_memory l_precomp l_script l_struct",
    "splines": "math_angles math_matrix math_quaternion math_vector q_parse splines util_str",
    "sys": "con_log sys_main sys_unix con_tty",
    "renderer": "tr_animation tr_backend tr_bsp tr_cmds tr_cmesh tr_curve tr_flares tr_font tr_image tr_image_bmp "
                "tr_image_jpg tr_image_pcx tr_image_png tr_image_tga tr_init tr_light tr_main tr_marks tr_mesh "
                "tr_model tr_model_iqm tr_noise tr_scene tr_shade tr_shade_calc tr_shader tr_shadows tr_sky "
                "tr_surface tr_world",
}
EXTRA = [os.path.join(H, n) for n in ("amix_glimp_zzgl.c", "amix_input.c", "amix_snd.c", "amix_compat.c", "amix_cxx.cpp")]
ZZSRC = [ZZDIR + "/zzgl/zzgl_enc.c", ZZDIR + "/zzgl/zzgl_amix.c"]
JPEG = sorted(os.path.join(C, "jpeg-8c", n) for n in os.listdir(os.path.join(C, "jpeg-8c"))
              if n.startswith("j") and n.endswith(".c") and n not in ("jmemansi.c", "jmemdos.c", "jmemmac.c",
              "jmemname.c", "jpegtran.c"))
ZLIB = [os.path.join(C, "zlib-1.2.11", n + ".c") for n in "adler32 crc32 inffast inflate inftrees zutil unzip ioapi".split()]

mk = open(os.path.join(TOP, "Makefile")).read().replace("\\\n", " ")


def modlist(var, extra):
    names = re.findall(r"\$\(B\)/\$\(BASEGAME\)/(\S+)\.o", re.search(r"^\s*%s\s*:?=\s*(.*)$" % var, mk, re.M).group(1))
    return names + extra


MODULES = [("cgame", "CGAMEDLL -DCGAME", "cgame.sp.m68k.so", modlist("Q3CGOBJ_", ["cgame/cg_syscalls"])),
           ("qagame", "GAMEDLL -DQAGAME", "qagame.sp.m68k.so", modlist("Q3GOBJ_", ["game/g_syscalls"])),
           ("ui", "UI", "ui.sp.m68k.so", modlist("Q3UIOBJ_", ["ui/ui_syscalls"]))]


def compile_one(src, o, flags, log):
    if os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(src):
        return True
    cc = CXX if src.endswith(".cpp") else CC
    cmd = [cc] + flags + CFLAGS + (CXXFLAGS if src.endswith(".cpp") else CFLAGS_C) + ["-c", src, "-o", o]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write("$ " + " ".join(cmd) + "\n" + r.stdout)
    return r.returncode == 0


def group(name, srcs, flags):
    d = os.path.join(H, "obj", name)
    os.makedirs(d, exist_ok=True)
    os.makedirs(os.path.join(H, "log"), exist_ok=True)
    log = open(os.path.join(H, "log", name + ".log"), "w")
    good, bad = [], []
    for s in srcs:
        o = os.path.join(d, os.path.basename(s).rsplit(".", 1)[0] + ".o")
        (good if compile_one(s, o, flags, log) else bad).append(o if os.path.exists(o) else s)
    bad = [b for b in bad]
    print("%-8s %3d ok %3d failed%s" % (name, len(good), len(bad),
          ": " + " ".join(os.path.basename(b) for b in bad[:12]) if bad else ""), flush=True)
    return good, bad


def modules():
    ok = True
    for name, define, so, names in MODULES:
        srcs = []
        for n in names:
            rel, base = (n.rsplit("/", 1) + [None])[:2]
            for sub in ([rel] if rel not in ("cgame", "game", "ui") else [rel, "game", "cgame", "ui", "qcommon"]):
                p = os.path.join(C, sub, base + ".c")
                if os.path.exists(p):
                    srcs.append(p)
                    break
            else:
                raise SystemExit("no source for " + n)
        objs, bad = group("mod-" + name, srcs + [os.path.join(H, "amix_compat.c")], ("-D" + define).split())
        if bad:
            ok = False
            continue
        bits = os.path.join(H, "obj", "libcbits")
        if not os.path.exists(os.path.join(bits, "fpsetrnd.o")):
            os.makedirs(bits, exist_ok=True)
            subprocess.run([BIN + "m68k-elf-ar", "x", SYSLIB + "/libc.a", "fpsetrnd.o", "fpsetsticky.o"], cwd=bits)
        gcclib = subprocess.run([CC, "-m68060", "-print-libgcc-file-name"], capture_output=True, text=True).stdout.strip()
        r = subprocess.run([LD, "-shared", "-o", os.path.join(H, so)] + objs + ["-L" + L060, "-lm060", "-lc060",
                           SYSLIB + "/libm.a", bits + "/fpsetrnd.o", bits + "/fpsetsticky.o", gcclib],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        open(os.path.join(H, "log", "link-" + name + ".log"), "w").write(r.stdout)
        print(so, "ok" if r.returncode == 0 else "LINK FAILED")
        ok = ok and r.returncode == 0
    return ok


def main():
    if "--clean" in sys.argv:
        subprocess.run(["rm", "-rf", os.path.join(H, "obj")])
    if "modules" in sys.argv:
        sys.exit(0 if modules() else 1)
    subprocess.run(["python3", "-I", ZZDIR + "/zzgl/gen.py", ZZDIR + "/zzgl/gen"], check=True)
    allobjs, failed = [], 0
    for rel, names in ENGINE.items():
        g, b = group(rel, [one(rel, n) for n in names.split()], ENGINE_FLAGS)
        allobjs += g
        failed += len(b)
    for name, srcs, fl in (("jpeg", JPEG, []), ("zlib", ZLIB, []), ("amix", EXTRA, ENGINE_FLAGS), ("zzgl", ZZSRC, ZZFLAGS)):
        g, b = group(name, srcs, fl)
        allobjs += g
        failed += len(b)
    if failed:
        sys.exit(1)
    out = os.path.join(H, "iowolfsp.m68k")
    r = subprocess.run([CC, "-O2", "-m68060", "-Wl,--allow-multiple-definition", "-o", out] + allobjs + ["-L" + X + "/lib", "-lXext", "-lX11",
                       "-lsocket", "-lnsl", "-ldl", "-L" + L060, "-lm060", "-lc060", "-lm"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    open(os.path.join(H, "log", "link.log"), "w").write(r.stdout)
    print("link:", "ok" if r.returncode == 0 else "FAILED (amix/log/link.log)")
    sys.exit(r.returncode)


main()
