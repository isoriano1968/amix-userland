#!/bin/sh
# vplay -- video player (FFmpeg 4.4 + X11R6.3 MIT-SHM + Paula) on the A3000.
#   vplay [-x2] [-noaudio] [-stereo] [-stats] [-nosync] file
LD_LIBRARY_PATH=/usr/x11r6/xc/exports/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}; export LD_LIBRARY_PATH
DISPLAY=${DISPLAY:-:0}; export DISPLAY
# the default 16 MB is too little for H.264 at 720p (reference pictures: get_buffer() failed)
ulimit -d 98304 2>/dev/null; ulimit -v 98304 2>/dev/null
exec /opt/vplay/vplay "$@"
