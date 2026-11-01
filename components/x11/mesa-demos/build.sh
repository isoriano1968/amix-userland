#!/bin/sh
# build.sh SRC SYSROOT OUT SET... -- mesa-demos (SRC) for AMIX
# against the Mesa 18 package in SYSROOT: glsl tests demos trivial ... in OUT/SET.
# They look for data in /opt/mesa-demos/data.
H=$(cd "$(dirname "$0")" && pwd)
SRC=$1; ST=$2; OUT=$3; shift 3
export PATH=$HOME/opt/m68k-amix/bin:$PATH
F="-O2 -m68060 -ffast-math -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion
 -Wno-error=incompatible-pointer-types -Wno-error=implicit-int -U__linux__ -Ulinux -D__amix__ -DSVR4
 -DRTLD_LOCAL=0 -DM_PI=3.14159265358979323846 -DM_PI_2=1.57079632679489661923 -DGLAPIENTRY= -DDEMOS_DATA_DIR=\"/opt/mesa-demos/data/\" -DHAVE_X11 -I$SRC/src/glad/include -I$SRC/src/util
 -I$ST/usr/local/include -I$ST/usr/x11r6/include"
O=$OUT/obj
mkdir -p "$O"
cc1() { m68k-elf-gcc $F "$@"; }
cc1 -DgladLoaderLoadGL=amix_unused_loader -c "$SRC/src/glad/src/gl.c" -o "$O/glad.o" || exit 1
cc1 -c "$H/amix-loader.c" -o "$O/loader.o" || exit 1
for u in trackball showbuffer shaderutil readtex matrix; do
	[ -f "$SRC/src/util/$u.c" ] && cc1 -DgladLoaderLoadGL=amix_gladLoaderLoadGL -c "$SRC/src/util/$u.c" -o "$O/$u.o"
done
ok=0; bad=0
for s in "$@"; do
	mkdir -p "$OUT/$s"
	for f in "$SRC/src/$s"/*.c; do
		b=$(basename "$f" .c)
		if m68k-elf-gcc $F -DgladLoaderLoadGL=amix_gladLoaderLoadGL -I"$SRC/src/$s" "$f" "$O"/*.o -o "$OUT/$s/$b" \
			-L"$ST/usr/local/lib" -lglut -lGLU -lGL -lpng16 -lz -L"$ST/usr/x11r6/lib" -lXmu -lXi -lXext -lX11 -lsocket -lnsl -lm \
			-Wl,-rpath-link,"$ST/usr/local/lib" -Wl,-rpath-link,"$ST/usr/x11r6/lib" -Wl,-R,/usr/local/lib:/usr/x11r6/lib \
			2> "$OUT/$s/$b.err"; then
			m68k-elf-strip "$OUT/$s/$b"; rm -f "$OUT/$s/$b.err"; ok=$((ok+1))
		else
			bad=$((bad+1)); rm -f "$OUT/$s/$b"
		fi
	done
done
echo "built $ok, failed $bad"
