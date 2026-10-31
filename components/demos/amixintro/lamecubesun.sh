#!/bin/sh
# lamecubesun -- LameCubeSUN, LameCubeAMIGA42 with a bouncing Sun logo (TinyGL,
# music by mikmod).   lamecubesun [module]    ESC or q quits
LD_LIBRARY_PATH=/usr/x11r6/lib/r63${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}; export LD_LIBRARY_PATH
DISPLAY=${DISPLAY:-:0}; export DISPLAY
cd /opt/lamercubeamix && exec ./LameCubeSUN "$@"
