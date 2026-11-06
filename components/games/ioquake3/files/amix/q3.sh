#!/bin/sh
# q3.sh [ioquake3 options] -- ioquake3 (Quake III Arena demo) with the GL on
# the Z3660's second ARM core (zzgl: Mesa 18.3 there, frames in the RTG
# overlay over the window), /opt/ioq3/q3.sh.  Core 1 must run the "amix"
# image (gpu + H.264 + sound); anything else there is replaced by it.
# Sound through the board's HDMI (Paula with +set s_hdmi 0).  F10 quits.
# (The Mesa/soft3D launcher is q3-mesa.sh in the repo; not installed.)
ulimit -d 196608
ulimit -v 262144
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep " amix," >/dev/null; then :; else
	$C/armbench -reload $C/z3660ARM_Core_amix.bin || exit 1
fi
cd /opt/ioq3
exec /opt/amix/bin/xrun ./ioquake3.zzgl +set fs_basepath /opt/ioq3 +set fs_homepath /opt/ioq3/home \
	+set com_basegame demoq3 +set com_hunkmegs 32 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
	+set s_initsound 1 +set s_mixahead 0.5 +set com_soundMegs 4 +set cg_drawFPS 1 +bind F10 quit \
	+set vm_cgame 0 +set vm_game 0 +set vm_ui 0 +set sv_pure 0 +set r_ext_multitexture 1 +set r_texturemode GL_LINEAR_MIPMAP_NEAREST "$@"
