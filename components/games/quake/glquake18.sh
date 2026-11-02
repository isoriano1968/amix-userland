#!/bin/sh
# glquake18.sh -- GLQuake on Mesa 18 and its Gallium driver, sound on the HDMI.
# /opt/quake/glquake18.sh; the game's data in /opt/quake/id1.
unset LD_LIBRARY_PATH
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " gpu," | grep -v " amix," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_gpu.bin >/dev/null 2>&1 || exit 1
fi
ulimit -d 98304
ulimit -v 131072
cd /opt/quake
GALLIUM_DRIVER=zz3660; export GALLIUM_DRIVER
exec /opt/amix/bin/xrun ./glquake.m18 -basedir /opt/quake +_snd_mixahead 0.3 "$@" -width 640 -height 480 -conwidth 640
