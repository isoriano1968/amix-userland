#!/bin/sh
# build-amixcast.sh -- the CDE app, on the shared Motif 2.3.8 and X11R6.3
# (desktop/motif, x11/x11r6.3) through pkg-config
set -e
T=$(cd "$(dirname "$0")/../../../../tools" && pwd)
cd "$(dirname "$0")"
m68k-elf-gcc -O2 -m68060 -fno-strict-aliasing -Wno-implicit-int -Wno-implicit-function-declaration \
  -Wno-incompatible-pointer-types -Wno-int-conversion -DFUNCPROTO=15 \
  $("$T/amix-pkg-config" --cflags xm) -o amixcast amixcast.c $("$T/amix-pkg-config" --libs xm)
ls -l amixcast
