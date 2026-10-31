#!/usr/bin/env python3
"""build.py -- ioquake3 for AMIX (A3000 + Z3660 68060), cross-built with
gcc14-cross-amix.  One static client binary: the GL1 renderer linked in
(no dlopen), Mesa 3.1's libGL (MESA_SOFT3D=1 draws on the Z3660's ARM),
X11 for the window and input (amix/amix_glimp.c, amix/amix_input.c; no SDL),
the QVM interpreter, the internal zlib and libjpeg, no OpenAL, curl, VoIP,
Ogg/Opus or mumble.

    python3 -I amix/build.py [--clean] [group ...]     (from the ioq3 tree)

Objects go to amix/obj/<group>/, logs to amix/log/, the binary to
amix/ioquake3.m68k.
"""
import os, subprocess, sys

H = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(H)
C = os.path.join(TOP, "code")
HOME = os.path.expanduser("~")
CC = HOME + "/opt/m68k-amix/bin/m68k-elf-gcc"
X = HOME + "/opt/m68k-amix-sysroot/usr/x11r6/xc/exports"
MESA = HOME + "/Sources/usr-amix/mesa31-amix"
L060 = HOME + "/Sources/usr-amix/lib060"

CFLAGS = ["-O2", "-m68060", "-fno-strict-aliasing", "-fcommon", "-w",
          "-std=gnu99", "-U__linux__", "-Ulinux", "-D__amix__", "-DSVR4",
          "-DBOTLIB", '-DPRODUCT_VERSION="1.36-amix"', "-DNO_GZIP",
          "-DUSE_INTERNAL_JPEG", "-DUSE_INTERNAL_ZLIB",
          "-fno-builtin-sin", "-fno-builtin-cos", "-fno-builtin-tan", "-fno-builtin-fmod",
          "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
          "-Wno-error=incompatible-pointer-types", "-Wno-error=implicit-int",
          "-Wno-error=return-mismatch",
          "-include", os.path.join(H, "include", "amix_compat.h"),
          "-I" + os.path.join(H, "include"),
          "-I" + os.path.join(C, "thirdparty", "jpeg-9f"),
          "-I" + os.path.join(C, "thirdparty", "zlib-1.3.1"),
          "-I" + MESA + "/include", "-I" + X + "/include"]

def files(d, names):
    return [os.path.join(d, n) for n in names]

GROUPS = [
    ("qcommon", files("qcommon", """cm_load.c cm_patch.c cm_polylib.c cm_test.c cm_trace.c
        cmd.c common.c cvar.c files.c md4.c md5.c msg.c net_chan.c net_ip.c huffman.c
        q_math.c q_shared.c unzip.c ioapi.c vm.c vm_interpreted.c""".split())),
    ("sys", files("sys", "con_log.c sys_autoupdater.c sys_main.c sys_unix.c con_tty.c".split())),
    ("server", files("server", """sv_bot.c sv_client.c sv_ccmds.c sv_game.c sv_init.c sv_main.c
        sv_net_chan.c sv_snapshot.c sv_world.c""".split())),
    ("botlib", files("botlib", """be_aas_bspq3.c be_aas_cluster.c be_aas_debug.c be_aas_entity.c
        be_aas_file.c be_aas_main.c be_aas_move.c be_aas_optimize.c be_aas_reach.c
        be_aas_route.c be_aas_routealt.c be_aas_sample.c be_ai_char.c be_ai_chat.c
        be_ai_gen.c be_ai_goal.c be_ai_move.c be_ai_weap.c be_ai_weight.c be_ea.c
        be_interface.c l_crc.c l_libvar.c l_log.c l_memory.c l_precomp.c l_script.c
        l_struct.c""".split())),
    ("client", files("client", """cl_cgame.c cl_cin.c cl_console.c cl_input.c cl_keys.c cl_main.c
        cl_net_chan.c cl_parse.c cl_scrn.c cl_ui.c cl_avi.c snd_adpcm.c snd_dma.c snd_mem.c
        snd_mix.c snd_wavelet.c snd_main.c snd_codec.c snd_codec_wav.c snd_openal.c""".split())),
    ("rcommon", files("renderercommon", """tr_font.c tr_image_bmp.c tr_image_jpg.c tr_image_pcx.c
        tr_image_png.c tr_image_pvr.c tr_image_tga.c tr_noise.c puff.c""".split())),
    ("gl1", files("renderergl1", """tr_animation.c tr_backend.c tr_bsp.c tr_cmds.c tr_curve.c
        tr_flares.c tr_image.c tr_init.c tr_light.c tr_main.c tr_marks.c tr_mesh.c tr_model.c
        tr_model_iqm.c tr_scene.c tr_shade.c tr_shade_calc.c tr_shader.c tr_shadows.c tr_sky.c
        tr_surface.c tr_world.c tr_amix_s3d.c""".split())),
    ("jpeg", sorted("thirdparty/jpeg-9f/" + n for n in os.listdir(os.path.join(C, "thirdparty/jpeg-9f"))
                    if n.startswith("j") and n.endswith(".c") and n not in ("jmemansi.c", "jmemdos.c",
                    "jmemmac.c", "jmemname.c", "jpegtran.c"))),
    ("zlib", sorted("thirdparty/zlib-1.3.1/" + n for n in os.listdir(os.path.join(C, "thirdparty/zlib-1.3.1"))
                    if n.endswith(".c") and not n.startswith("gz"))),
    ("amix", ["../amix/amix_glimp.c", "../amix/amix_input.c", "../amix/amix_snd.c",
              "../amix/amix_compat.c", "../amix/amix_glprocs.c"]),
]

# ZZGL=1: GL on the Z3660's core 1 (z3660gl-amix's zzgl) in place of Mesa
# 3.1 and soft3D: amix_glimp_zzgl.c, the encoder and the AMIX side of zzgl,
# no libGL; objects in obj-zzgl, the program ioquake3.zzgl
ZZGL = os.environ.get("ZZGL") == "1"
ZZDIR = HOME + "/Sources/usr-amix/z3660gl-amix"
if ZZGL:
    CFLAGS += ["-DAMIX_ZZGL", "-I" + ZZDIR + "/zzgl"]
    GROUPS = [(g, [x for x in srcs if not x.endswith(("tr_amix_s3d.c", "amix_glprocs.c"))])
              for g, srcs in GROUPS]
    GROUPS = [(g, [x.replace("amix_glimp.c", "amix_glimp_zzgl.c") for x in srcs]) for g, srcs in GROUPS]
    GROUPS.append(("zzgl", [ZZDIR + "/zzgl/zzgl_enc.c", ZZDIR + "/zzgl/zzgl_amix.c"]))
# the zzgl sources see Mesa 18.3's GL headers and vplay's board layout
GROUP_FLAGS = {"zzgl": ["-I" + ZZDIR + "/src/mesa-18.3.6/include", "-I" + ZZDIR + "/compat/amix",
                        "-I" + HOME + "/Sources/usr-amix/vplay-amix/arm/h264bench"]}
OBJ = "obj-zzgl" if ZZGL else "obj"

LIBS = ["-L" + MESA + "/shm-static", "-L" + MESA + "/lib", "-L" + X + "/lib", "-lGL", "-lXext", "-lX11",
        "-lshmsys", "-lsocket", "-lnsl", "-ldl", "-L" + L060, "-lm060", "-lc060", "-lm"]
if ZZGL:
    LIBS = ["-L" + X + "/lib", "-lXext", "-lX11", "-lsocket", "-lnsl", "-ldl",
            "-L" + L060, "-lm060", "-lc060", "-lm"]


def build(group, srcs):
    obj = os.path.join(H, OBJ, group)
    os.makedirs(obj, exist_ok=True)
    os.makedirs(os.path.join(H, "log"), exist_ok=True)
    log = open(os.path.join(H, "log", group + ".log"), "w")
    objs, bad = [], []
    for s in srcs:
        src = os.path.normpath(os.path.join(C, s))
        o = os.path.join(obj, os.path.basename(s)[:-2] + ".o")
        if os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(src):
            objs.append(o)
            continue
        cmd = [CC] + GROUP_FLAGS.get(group, []) + CFLAGS + ["-c", src, "-o", o]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write("$ " + " ".join(cmd) + "\n" + r.stdout)
        (objs if r.returncode == 0 else bad).append(o if r.returncode == 0 else s)
    print("%-8s %3d ok %3d failed%s" % (group, len(objs), len(bad),
          (": " + " ".join(os.path.basename(b) for b in bad[:10])) if bad else ""))
    return objs, bad


# the game modules, native: <name>m68k.so for the engine's dlopen (vm_cgame 0
# etc.); bg_lib's replacements stay out (Q3_VM only), libm and lib060 go in
MODULES = [
    ("cgame", "CGAME", """cgame/cg_main.c game/bg_misc.c game/bg_pmove.c game/bg_slidemove.c game/bg_lib.c
        cgame/cg_consolecmds.c cgame/cg_draw.c cgame/cg_drawtools.c cgame/cg_effects.c cgame/cg_ents.c
        cgame/cg_event.c cgame/cg_info.c cgame/cg_localents.c cgame/cg_marks.c cgame/cg_particles.c
        cgame/cg_players.c cgame/cg_playerstate.c cgame/cg_predict.c cgame/cg_scoreboard.c
        cgame/cg_servercmds.c cgame/cg_snapshot.c cgame/cg_view.c cgame/cg_weapons.c cgame/cg_syscalls.c
        qcommon/q_math.c qcommon/q_shared.c"""),
    ("qagame", "QAGAME", """game/g_main.c game/ai_chat.c game/ai_cmd.c game/ai_dmnet.c game/ai_dmq3.c
        game/ai_main.c game/ai_team.c game/ai_vcmd.c game/bg_misc.c game/bg_pmove.c game/bg_slidemove.c
        game/bg_lib.c game/g_active.c game/g_arenas.c game/g_bot.c game/g_client.c game/g_cmds.c
        game/g_combat.c game/g_items.c game/g_mem.c game/g_misc.c game/g_missile.c game/g_mover.c
        game/g_session.c game/g_spawn.c game/g_svcmds.c game/g_target.c game/g_team.c game/g_trigger.c
        game/g_utils.c game/g_weapon.c game/g_syscalls.c qcommon/q_math.c qcommon/q_shared.c"""),
    ("ui", "UI", """q3_ui/ui_main.c game/bg_misc.c game/bg_lib.c q3_ui/ui_addbots.c q3_ui/ui_atoms.c
        q3_ui/ui_cdkey.c q3_ui/ui_cinematics.c q3_ui/ui_confirm.c q3_ui/ui_connect.c q3_ui/ui_controls2.c
        q3_ui/ui_credits.c q3_ui/ui_demo2.c q3_ui/ui_display.c q3_ui/ui_gameinfo.c q3_ui/ui_ingame.c
        q3_ui/ui_loadconfig.c q3_ui/ui_menu.c q3_ui/ui_mfield.c q3_ui/ui_mods.c q3_ui/ui_network.c
        q3_ui/ui_options.c q3_ui/ui_playermodel.c q3_ui/ui_players.c q3_ui/ui_playersettings.c
        q3_ui/ui_preferences.c q3_ui/ui_qmenu.c q3_ui/ui_removebots.c q3_ui/ui_saveconfig.c
        q3_ui/ui_serverinfo.c q3_ui/ui_servers2.c q3_ui/ui_setup.c q3_ui/ui_sound.c q3_ui/ui_sparena.c
        q3_ui/ui_specifyserver.c q3_ui/ui_splevel.c q3_ui/ui_sppostgame.c q3_ui/ui_spskill.c
        q3_ui/ui_startserver.c q3_ui/ui_team.c q3_ui/ui_teamorders.c q3_ui/ui_video.c ui/ui_syscalls.c
        qcommon/q_math.c qcommon/q_shared.c"""),
]
LD = HOME + "/opt/m68k-amix/bin/m68k-elf-ld"
SYSLIB = HOME + "/opt/m68k-amix-sysroot/usr/ccs/lib"
GCCLIB = subprocess.run([CC, "-m68060", "-print-libgcc-file-name"], capture_output=True, text=True).stdout.strip()


def build_module(name, define):
    srcs = MODULES[[m[0] for m in MODULES].index(name)][2].split()
    obj = os.path.join(H, "obj", "mod-" + name)
    os.makedirs(obj, exist_ok=True)
    log = open(os.path.join(H, "log", "mod-" + name + ".log"), "w")
    objs, bad = [], []
    for s in srcs:
        src = os.path.join(C, s)
        o = os.path.join(obj, os.path.basename(s)[:-2] + ".o")
        if not (os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(src)):
            # NOT -fPIC: gcc 14's m68k PIC code splits a pointer load from a
            # GOT slot into halves and addresses the second half as a GOT
            # entry for "symbol+2" -- a different slot (string literals came
            # out as other strings).  Absolute code, relocated by ld.so at
            # load (TEXTREL), is right and also faster.
            cmd = [CC] + CFLAGS + ["-D" + define, "-c", src, "-o", o]
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            log.write("$ " + " ".join(cmd) + "\n" + r.stdout)
            if r.returncode:
                bad.append(s)
                continue
        objs.append(o)
    out = os.path.join(H, name + "m68k.so")
    if not bad:
        # vsnprintf etc. from the compat layer (PIC), and the two libc.a
        # members libm.a calls that libc.so.1 does not export
        compat = os.path.join(obj, "amix_compat.o")
        subprocess.run([CC] + CFLAGS + ["-c", os.path.join(H, "amix_compat.c"), "-o", compat],
                       stdout=log, stderr=subprocess.STDOUT)
        bits = os.path.join(H, "obj", "libcbits")
        if not os.path.exists(os.path.join(bits, "fpsetrnd.o")):
            os.makedirs(bits, exist_ok=True)
            subprocess.run([HOME + "/opt/m68k-amix/bin/m68k-elf-ar", "x", SYSLIB + "/libc.a",
                            "fpsetrnd.o", "fpsetsticky.o"], cwd=bits)
        cmd = [LD, "-shared", "-o", out] + objs + [compat, "-L" + L060, "-lm060", "-lc060",
               SYSLIB + "/libm.a", os.path.join(bits, "fpsetrnd.o"), os.path.join(bits, "fpsetsticky.o"),
               GCCLIB]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write("$ " + " ".join(cmd) + "\n" + r.stdout)
        if r.returncode:
            bad.append("(link)")
    print("%-8s %3d ok %3d failed%s" % (name + ".so", len(objs), len(bad),
          (": " + " ".join(os.path.basename(b) for b in bad[:10])) if bad else ""))
    return not bad


def main():
    if sys.argv[1:2] == ["modules"]:
        os.makedirs(os.path.join(H, "log"), exist_ok=True)
        ok = all([build_module(n, d) for n, d, _ in MODULES])
        sys.exit(0 if ok else 1)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--clean" in sys.argv:
        subprocess.run(["rm", "-rf", os.path.join(H, "obj")])
    allobjs, failed = [], 0
    if ZZGL:
        subprocess.run(["python3", "-I", ZZDIR + "/zzgl/gen.py", ZZDIR + "/zzgl/gen"], check=True)
    for g, srcs in GROUPS:
        if args and g not in args:
            continue
        objs, bad = build(g, srcs)
        allobjs += objs
        failed += len(bad)
    if failed or args:
        sys.exit(1 if failed else 0)
    out = os.path.join(H, "ioquake3.zzgl" if ZZGL else "ioquake3.m68k")
    r = subprocess.run([CC, "-O2", "-m68060", "-o", out] + allobjs + LIBS,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    open(os.path.join(H, "log", "link.log"), "w").write(r.stdout)
    print("link:", "ok" if r.returncode == 0 else "FAILED (amix/log/link.log)")
    sys.exit(r.returncode)


main()
