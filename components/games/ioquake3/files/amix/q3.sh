#!/bin/sh
# q3.sh [ioquake3 options] -- ioquake3 (Quake III Arena demo) on AMIX, /opt/ioq3/q3.sh:
# 640x480 (the UI's own size: readable text), Mesa on the Z3660's soft3D
# (MESA_SOFT3D=0 for the 060), sound through Paula (/dev/paula), frame rate shown.
# Memory: 32 MB hunk and 4 MB of sounds -- with 48 + 8 the kernel ran short of
# contiguous pages (hat_sdtalloc) while the map loaded; 128 MB, no swap.
ulimit -d 196608
ulimit -v 262144
MESA_SOFT3D=${MESA_SOFT3D-1}; export MESA_SOFT3D
cd /opt/ioq3
exec /opt/amix/bin/xrun ./ioquake3 +set fs_basepath /opt/ioq3 +set fs_homepath /opt/ioq3/home \
	+set com_basegame demoq3 +set com_hunkmegs 32 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
	+set s_initsound 1 +set s_mixahead 0.8 +set com_soundMegs 4 +set cg_drawFPS 1 +set vm_cgame 0 +set vm_game 0 +set vm_ui 0 +set sv_pure 0 +set r_ext_multitexture 0 "$@"
