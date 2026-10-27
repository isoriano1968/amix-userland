#!/bin/sh
# lamercubeamix -- LamerCubeAMIX, the oldskool AMIX intro (Mesa 3.1 + GLUT,
# music by mikmod).   lamercubeamix [module]      ESC or q quits
LD_LIBRARY_PATH=/usr/x11r6/lib/r63${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}; export LD_LIBRARY_PATH
DISPLAY=${DISPLAY:-:0}; export DISPLAY
cd /opt/lamercubeamix && exec ./LamerCubeAMIX "$@"
