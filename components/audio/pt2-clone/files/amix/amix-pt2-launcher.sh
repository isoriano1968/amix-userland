#!/bin/sh
# pt2 -- ProTracker 2.3D clone (pt2-clone + sdl2-amix) under X on the A3000.
#   pt2 [module]        PT2_SCALE=1..3 (window 320x255 times it, default 2)
# Sound: /dev/audio, locked stereo, 27710 Hz (PT2_PAULA_PERIOD, default 128).
DISPLAY=${DISPLAY:-:0}; export DISPLAY
exec /opt/pt2/pt2-clone "$@"
