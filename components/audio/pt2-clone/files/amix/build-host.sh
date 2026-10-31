#!/bin/sh
# build-host.sh -- the same pt2-clone + sdl2-amix, built for a Linux PC (Xlib),
# to test the SDL layer under Xephyr:  Xephyr :5 -screen 1280x800x16 &
#   DISPLAY=:5 PT2_NULLAUDIO=1 ./amix/pt2-clone-host
# -D__amix__ selects the thread-free paths; the host is little-endian, so the
# big-endian code is not exercised here.
set -e
H=$(cd "$(dirname "$0")" && pwd)
PT2=${PT2_SRC:-$H/..}
O=$H/obj-host; mkdir -p "$O"
for f in "$PT2"/src/*.c "$PT2"/src/gfx/*.c "$PT2"/src/modloaders/*.c "$PT2"/src/smploaders/*.c "$H"/sdl2/sdl2amix.c; do
	gcc -O1 -g -D__amix__ -DNDEBUG -I"$H/sdl2/include" -w -c "$f" -o "$O/$(basename "$f" .c).o"
done
gcc -o "$H/pt2-clone-host" "$O"/*.o -lXext -lX11 -lm
ls -l "$H/pt2-clone-host"
