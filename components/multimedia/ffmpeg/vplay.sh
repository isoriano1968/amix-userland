#!/bin/sh
# vplay -- video player (FFmpeg 4.4 + X11R6.3 MIT-SHM + Paula) on the A3000.
#   vplay [-x2] [-noaudio] [-stereo] [-stats] [-nosync] file
LD_LIBRARY_PATH=/usr/x11r6/xc/exports/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}; export LD_LIBRARY_PATH
DISPLAY=${DISPLAY:-:0}; export DISPLAY
# core 1: the amix core (the player, GL and crypto) when a core without the
# player runs there
C=/root/z3660ARMCores
if [ -x /usr/amiga/bin/ztop ] && /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " h264bench," | grep -v " amix," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_amix.bin >/dev/null 2>&1
fi
# the default 16 MB is too little for H.264 at 720p (reference pictures: get_buffer() failed)
ulimit -d 98304 2>/dev/null; ulimit -v 98304 2>/dev/null
exec /opt/vplay/vplay "$@"
