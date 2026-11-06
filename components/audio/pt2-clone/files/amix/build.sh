#!/bin/sh
# build.sh -- pt2-clone for AMIX, cross-built on a Linux host with
# gcc14-cross-amix for the 68060, with sdl2-amix and X11R6.3:
#   sh amix/build.sh            -> amix/pt2-clone (an AMIX ELF)
#   AMIX_SYSROOT  the sysroot with the shared X11R6.3 in usr/x11r6
#                 (default ~/opt/m68k-amix-sysroot)
#   SHM_LIBS      directory with libshmsys.a (the shared libXext has XShm)
#   AMIX_GATE     amix-gate (default beside amix-userland): core 1's zzgl.h and
#                 bench.h for the HDMI sound
#   m68k-elf-gcc must be on PATH.
# zzhdmi/: library/sdl2's HDMI sound helper (the component's Makefile copies it).
set -e
H=$(cd "$(dirname "$0")" && pwd)
PT2=${PT2_SRC:-$H/..}
X=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6
ZZ=$H/zzhdmi
C1=${AMIX_GATE:-$H/../../../../../../../amix-gate}/usr/src/firmware/core1
[ -f "$ZZ/zzhdmi_board.c" ] || { echo "no $ZZ/zzhdmi_board.c (library/sdl2's)"; exit 1; }
[ -f "$C1/zzgl/zzgl.h" ] || { echo "no amix-gate's core 1 headers in $C1 (AMIX_GATE)"; exit 1; }
[ -n "$SHM_LIBS" ] && [ -f "$SHM_LIBS/libshmsys.a" ] || {
	echo "SHM_LIBS must name a directory with libshmsys.a"; exit 1; }
CC=m68k-elf-gcc
# -Wno-error=incompatible-pointer-types: here int32_t is long, SDL (and qsort)
# use int -- the same 32 bits; gcc 14 only objects to the spelling.
CFLAGS="-O2 -m68060 -DNDEBUG -ffast-math -fno-strict-aliasing -include $H/compat/amix-c99.h
	-I$H/sdl2/include -I$ZZ -I$X/include -Wno-unused-result -Wno-error=incompatible-pointer-types"
O=$H/obj; mkdir -p "$O"
fail=0
for f in "$PT2"/src/*.c "$PT2"/src/gfx/*.c "$PT2"/src/modloaders/*.c "$PT2"/src/smploaders/*.c "$H"/sdl2/sdl2amix.c "$H"/compat/amix-c99.c; do
	o=$O/$(basename "$f" .c).o	# everything, every time: the headers change too
	$CC $CFLAGS -c "$f" -o "$o" 2> "$o.log" || { echo "FAIL $f"; sed -n 1,15p "$o.log"; fail=1; }
done
o=$O/zzhdmi_board.o
$CC $CFLAGS -I$C1/zzgl -I$C1/cores/h264bench -c "$ZZ/zzhdmi_board.c" -o "$o" 2> "$o.log" || { echo "FAIL zzhdmi_board.c"; cat "$o.log"; fail=1; }
# libc replacements: gcc must not turn their loops back into calls to themselves
o=$O/amix-mem.o
$CC -O2 -fno-builtin -fno-tree-loop-distribute-patterns -c "$H/compat/amix-mem.c" -o "$o" 2> "$o.log" || { echo "FAIL amix-mem.c"; cat "$o.log"; fail=1; }
[ $fail = 0 ] || exit 1
# the shared X libraries first: SHM_LIBS may also hold an old static libXext.a
$CC -o "$H/pt2-clone" "$O"/*.o -L"$X/lib" -Wl,-rpath,/usr/x11r6/lib -L"$SHM_LIBS" -lXext -lX11 -lshmsys -lsocket -lnsl -lm
ls -l "$H/pt2-clone"
