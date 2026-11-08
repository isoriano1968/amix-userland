#!/bin/sh
# wolf-zzgl.sh [iortcw options] -- Return to Castle Wolfenstein with the GL on the
# Z3660's second ARM core (zzgl), sound over HDMI.  /opt/rtcw/wolf.sh; the
# game's paks (amix/trimpaks.py reduces them) in /opt/rtcw/main.
# Core 1 must run the "amix" image; anything else there is replaced by it.  F10 quits.
ulimit -d 262144
ulimit -v 393216
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep " amix," >/dev/null; then :; else
	$C/armbench -reload $C/z3660ARM_Core_amix.bin || exit 1
fi
[ $# -gt 0 ] || set -- +spmap escape1	# spmap: single player, devmap is multiplayer: no AI, no scripts
cd /opt/rtcw
# the game leaves the terminal without echo when it is stopped: restore it
T=`stty -g 2>/dev/null`
/opt/amix/bin/xrun ./iowolfsp.m68k +set fs_basepath /opt/rtcw +set fs_homepath /opt/rtcw/home \
	+set com_hunkmegs 64 +set com_recommendedSet 1 +set r_texturemode GL_LINEAR_MIPMAP_NEAREST +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
	+set s_initsound 1 +set s_mixahead 0.8 +set com_soundMegs 4 +set cg_drawFPS 1 +bind F10 quit \
	+set vm_cgame 0 +set vm_game 0 +set vm_ui 0 +set sv_pure 0 +set r_ext_multitexture 1 "$@"
[ -n "$T" ] && stty "$T"
