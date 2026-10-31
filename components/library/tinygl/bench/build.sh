#!/bin/sh
# build.sh -- the TinyGL benchmarks for AMIX (68060, MIT-SHM, static X client libs)
#   texbench.tinygl  textured/flat cube fill rate (texbench.c)
#   blendtest        blending, alpha test, depth mask, texenv on soft3D (blendtest.c)
set -e
cd "$(dirname "$0")"
CC=$HOME/opt/m68k-amix/bin/m68k-elf-gcc
X=$HOME/opt/m68k-amix-sysroot/usr/x11r6/xc/exports
M=$HOME/Sources/usr-amix/amix-userland/components/x11/mesa-31/work
CFLAGS="-O2 -m68060 -ffast-math -fno-builtin-sin -fno-builtin-cos -fno-builtin-tan -fno-builtin-fmod -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-tanf -fno-builtin-fmodf -w -D__amix__ -U__linux__ -Ulinux -I../include -I$X/include"
LIBS="-L$M/shm-static -L$X/lib -lXext -lX11 -lshmsys -lsocket -lnsl -L$HOME/Sources/usr-amix/lib060 -lm060 -lc060 -lm"
$CC $CFLAGS -o texbench.tinygl texbench.c -L../lib -lTinyGL $LIBS
$CC $CFLAGS -o blendtest blendtest.c -L../lib -lTinyGLUT -lTinyGL $LIBS
