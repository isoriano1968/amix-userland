#!/bin/sh
# wolf.sh [iortcw options] -- Return to Castle Wolfenstein with the GL on the
# Z3660's second ARM core (zzgl), sound over HDMI.  /opt/rtcw/wolf.sh; the
# game's paks (amix/trimpaks.py reduces them) in /opt/rtcw/main.
ulimit -d 196608
ulimit -v 262144
LD_LIBRARY_PATH=/usr/x11r6/lib/r63; export LD_LIBRARY_PATH
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " gpu," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_gpu.bin || exit 1
fi
[ $# -gt 0 ] || set -- +spmap escape1	# spmap: single player, devmap is multiplayer: no AI, no scripts
cd /opt/rtcw
exec /opt/amix/bin/xrun ./iowolfsp.m68k +set fs_basepath /opt/rtcw +set fs_homepath /opt/rtcw/home \
	+set com_hunkmegs 64 +set com_recommendedSet 1 +set r_texturemode GL_LINEAR_MIPMAP_NEAREST +set r_mode -1 +set r_customwidth 512 +set r_customheight 384 \
	+set s_initsound 0 +set s_mixahead 0.8 +set com_soundMegs 8 +set cg_drawFPS 1 \
	+set vm_cgame 0 +set vm_game 0 +set vm_ui 0 +set sv_pure 0 +set r_ext_multitexture 1 "$@"
