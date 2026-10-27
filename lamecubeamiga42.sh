#!/bin/sh
# lamecubeamiga42 -- LameCubeAMIGA42, the second oldskool AMIX intro (TinyGL,
# music by mikmod).   lamecubeamiga42 [module]    ESC or q quits
LD_LIBRARY_PATH=/usr/x11r6/lib/r63${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}; export LD_LIBRARY_PATH
DISPLAY=${DISPLAY:-:0}; export DISPLAY
cd /opt/lamercubeamix && exec ./LameCubeAMIGA42 "$@"
