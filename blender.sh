#!/bin/sh
# blender -- Blender 2.31a for AMIX (no Python, game engine or sound).
#   blender [file.blend]         opens a 1024x700 window at 0,0
#   blender -p X Y W H [...]     any other window geometry
DISPLAY=${DISPLAY:-:0}; export DISPLAY
cd /opt/blender
case " $* " in
*" -p "*|*" -w "*|*" -W "*) exec ./blender "$@" ;;
*) exec ./blender -p 0 0 1024 700 "$@" ;;
esac
