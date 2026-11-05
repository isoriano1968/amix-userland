#!/bin/sh
# build-amixcast.sh -- the CDE app, on the shared Motif 2.3.8 and X11R6.3
# (desktop/motif, x11/x11r6.3) and libupnp (library/libupnp) through
# pkg-config.  dlna.c takes GNU Pth's pthread.h, as libupnp does;
# amixcast.c keeps the plain socket calls.
set -e
T=$(cd "$(dirname "$0")/../../../../tools" && pwd)
P=$T/amix-pkg-config
cd "$(dirname "$0")"
F="-O2 -m68060 -fno-strict-aliasing -Wno-implicit-int -Wno-implicit-function-declaration \
  -Wno-incompatible-pointer-types -Wno-int-conversion -DFUNCPROTO=15"
m68k-elf-gcc $F $($P --variable=pth_cflags pth) $($P --cflags libupnp) -c dlna.c -o dlna.o
m68k-elf-gcc $F $($P --cflags xm) -I../../../multimedia/ffmpeg/arm/h264bench -c amixcast.c -o amixcast.o
m68k-elf-gcc -o amixcast amixcast.o dlna.o $($P --libs libupnp pth xm)
rm -f amixcast.o dlna.o
ls -l amixcast
