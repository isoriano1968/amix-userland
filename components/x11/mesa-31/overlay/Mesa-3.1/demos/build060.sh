#!/bin/sh
# build060.sh [demo ...] -- the Mesa 3.1 demos for the 68060 (gcc14), static,
# against ../lib (libGL with the soft3D driver; MESA_SOFT3D=0 is off), MIT-SHM and
# lib060 (fast sin/cos/memcpy).  Output in bin060/.
set -e
cd "$(dirname "$0")"
CC=$HOME/opt/m68k-amix/bin/m68k-elf-gcc
X=$HOME/opt/m68k-amix-sysroot/usr/x11r6/xc/exports
M=..
L060=$HOME/Sources/usr-amix/amix-userland/components/library/lib060
mkdir -p bin060 xmu-static
ln -sf $X/lib/libXmu.a xmu-static/libXmu.a
CFLAGS="-O2 -m68060 -ffast-math -w -U__linux__ -Ulinux -D__amix__ -DSVR4 -I$M/include -I$X/include"
LIBS="-L$M/shm-static -Lxmu-static -L$M/lib -L$X/lib -lglut -lGLU -lGL -lXmu -lXext -lX11 -lshmsys -lsocket -lnsl -L$L060 -lm060 -lc060 -lm"
for d in ${*:-gears texobj morph3d isosurf gloss reflect texcyl bounce trispd clearspd spectex stex3d glutmech}; do
	$CC $CFLAGS -o bin060/$d $d.c $LIBS && echo "built $d" || echo "FAILED $d"
done
