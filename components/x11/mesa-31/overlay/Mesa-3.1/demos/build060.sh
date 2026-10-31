#!/bin/sh
# build060.sh [demo ...] -- the Mesa 3.1 demos for the 68060, cross-built with
# gcc14-cross-amix, static, against ../lib from "make amix060" (soft3D on the
# Z3660 unless MESA_SOFT3D=0, MIT-SHM).  Output in bin060/.
#   AMIX_SYSROOT  the sysroot with X11R6.3's exports (default ~/opt/m68k-amix-sysroot)
#   SHM_LIBS      where libXext.a with MIT-SHM and libshmsys.a are, if not in the exports
#   LIB060        optional: a directory with libm060.a/libc060.a (faster maths/memcpy)
set -e
cd "$(dirname "$0")"
CC=${CC:-m68k-elf-gcc}
X=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6/xc/exports
M=..
mkdir -p bin060 xmu-static
ln -sf "$X/lib/libXmu.a" xmu-static/libXmu.a
CFLAGS="-O2 -m68060 -ffast-math -w -U__linux__ -Ulinux -D__amix__ -DSVR4 -I$M/include -I$X/include"
EXTRA=""
[ -n "$LIB060" ] && EXTRA="-L$LIB060 -lm060 -lc060"
LIBS="${SHM_LIBS:+-L$SHM_LIBS} -Lxmu-static -L$M/lib -L$X/lib -lglut -lGLU -lGL -lXmu -lXext -lX11 -lshmsys -lsocket -lnsl $EXTRA -lm"
for d in ${*:-gears texobj morph3d isosurf gloss reflect texcyl bounce trispd clearspd spectex stex3d glutmech}; do
	$CC $CFLAGS -o bin060/$d $d.c $LIBS && echo "built $d" || echo "FAILED $d"
done
