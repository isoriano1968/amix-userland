#!/bin/sh
# glquake.sh -- GLQuake with the GL on the Z3660's core 1, sound on the HDMI.
# /opt/quake/glquake.sh; the game's data in /opt/quake/id1.
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " gpu," | grep -v " amix," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_amix.bin >/dev/null 2>&1 || exit 1
fi
ulimit -d 98304
ulimit -v 131072
cd /opt/quake
exec /opt/amix/bin/xrun ./glquake.zzgl -basedir /opt/quake +_snd_mixahead 0.3 "$@" -width 640 -height 480 -conwidth 640
