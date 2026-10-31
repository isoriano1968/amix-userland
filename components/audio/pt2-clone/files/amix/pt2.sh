#!/bin/sh
# pt2 [module] -- ProTracker 2.3D clone (pt2-clone + sdl2-amix) under X on AMIX.
# Install as e.g. /opt/amix/bin/pt2 with the program in /opt/pt2/pt2-clone.
# Sound: /dev/paula when it is there (Paula itself), else /dev/audio.
LD_LIBRARY_PATH=/usr/x11r6/xc/exports/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}; export LD_LIBRARY_PATH
DISPLAY=${DISPLAY:-:0}; export DISPLAY
exec ${PT2_PROGRAM:-/opt/pt2/pt2-clone} "$@"
