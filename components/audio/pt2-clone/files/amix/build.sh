#!/bin/sh
# build.sh -- pt2-clone for AMIX, cross-built on a Linux host with
# gcc14-cross-amix for the 68060, with sdl2-amix and X11R6.3:
#   sh amix/build.sh            -> amix/pt2-clone (an AMIX ELF)
#   AMIX_SYSROOT  the sysroot with X11R6.3's exports (default ~/opt/m68k-amix-sysroot)
#   SHM_LIBS      directory with a static libXext.a that has the XShm client
#                 functions, and libshmsys.a (see README.md)
#   m68k-elf-gcc must be on PATH.
set -e
H=$(cd "$(dirname "$0")" && pwd)
PT2=${PT2_SRC:-$H/..}
X=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6/xc/exports
[ -n "$SHM_LIBS" ] && [ -f "$SHM_LIBS/libXext.a" ] && [ -f "$SHM_LIBS/libshmsys.a" ] || {
	echo "SHM_LIBS must name a directory with libXext.a (with XShm) and libshmsys.a"; exit 1; }
CC=m68k-elf-gcc
# -Wno-error=incompatible-pointer-types: here int32_t is long, SDL (and qsort)
# use int -- the same 32 bits; gcc 14 only objects to the spelling.
CFLAGS="-O2 -m68060 -DNDEBUG -ffast-math -fno-strict-aliasing -include $H/compat/amix-c99.h
	-I$H/sdl2/include -I$X/include -Wno-unused-result -Wno-error=incompatible-pointer-types"
O=$H/obj; mkdir -p "$O"
fail=0
for f in "$PT2"/src/*.c "$PT2"/src/gfx/*.c "$PT2"/src/modloaders/*.c "$PT2"/src/smploaders/*.c "$H"/sdl2/sdl2amix.c "$H"/compat/amix-c99.c; do
	o=$O/$(basename "$f" .c).o	# everything, every time: the headers change too
	$CC $CFLAGS -c "$f" -o "$o" 2> "$o.log" || { echo "FAIL $f"; sed -n 1,15p "$o.log"; fail=1; }
done
# libc replacements: gcc must not turn their loops back into calls to themselves
o=$O/amix-mem.o
$CC -O2 -fno-builtin -fno-tree-loop-distribute-patterns -c "$H/compat/amix-mem.c" -o "$o" 2> "$o.log" || { echo "FAIL amix-mem.c"; cat "$o.log"; fail=1; }
[ $fail = 0 ] || exit 1
$CC -o "$H/pt2-clone" "$O"/*.o -L"$SHM_LIBS" -L"$X/lib" -lXext -lX11 -lshmsys -lsocket -lnsl -lm
ls -l "$H/pt2-clone"
