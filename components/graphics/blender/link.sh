#!/bin/sh
# link.sh -- link Blender 2.31a for AMIX from build.py's libraries.
set -e
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
X=$HOME/opt/m68k-amix-sysroot/usr/x11r6/xc/exports
MESA=$HOME/Sources/usr-amix/amix-userland/components/x11/mesa-31/work
L060=$HOME/Sources/usr-amix/x11r63-amix/lib060
CF="-I$H/amixinc -O2 -m68060 -funsigned-char -U__linux__ -Ulinux -D__amix__ -D__BIG_ENDIAN__ -std=gnu89 -include $H/ccompat.h -w"
mkdir -p $H/obj/link
m68k-elf-gcc $CF -c $H/obj/makesdna/DNA.c -o $H/obj/link/DNA.o
m68k-elf-gcc $CF -c $H/amixsrc/bpy_stub.c -o $H/obj/link/bpy_stub.o
for c in $H/amixsrc/*.c; do
	n=$(basename $c .c); [ $n = bpy_stub ] && continue
	m68k-elf-gcc $CF -I$H/src/source/blender/include -I$H/src/source/blender/makesdna -c $c -o $H/obj/link/$n.o
done
LIBS=$(ls $H/lib/lib*.a)
m68k-elf-g++ -o $H/blender $H/obj/link/*.o \
	-Wl,--start-group $LIBS \
	$H/deps/lib/libpng.a $H/deps/lib/libjpeg.a $H/deps/lib/libz.a \
	-L$MESA/lib -lGLU -lGL \
	-L$MESA/shm-static -L$L060 -lXmu -lXext -lX11 -lshmsys \
	-Wl,--end-group -ldl -lsocket -lnsl -lm 2> $H/log/link.log || true
grep -o "undefined reference to \`[^']*'" $H/log/link.log | sort | uniq -c | sort -rn | head -60
ls -l $H/blender 2>/dev/null || true
