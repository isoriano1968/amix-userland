#!/bin/sh
# build-shared.sh STAGE -- Mesa 3.1 as libGL, libGLU and libglut .so.3 in
# STAGE/usr/local/lib: the amix060 build of work/ (soft3D, MIT-SHM), -fPIC, in
# work-pic/.  Against the shared X of x11/x11r6.3's build-shared.sh in STAGE.
# The program links libshmsys.a itself (libc.so.1 has no shm*).
set -e
STAGE=$1
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
XL=$STAGE/usr/x11r6/lib
L=$STAGE/usr/local/lib
rm -rf "$H/work-pic"; cp -a "$H/work" "$H/work-pic"
cd "$H/work-pic"
for d in src src-glu src-glut; do
	rm -f $d/*.o
done
rm -f lib/lib*.a
for d in src src-glu src-glut; do
	touch $d/depend
	(cd $d && make -f Makefile.X11 amix060 AMIX_PIC=-fPIC) > build-$d.log 2>&1 || { echo "failed: $d, see build-$d.log"; exit 1; }
done
"$H/../../../tools/amix-got-check" src/*.o src-glu/*.o src-glut/*.o
mkdir -p "$L/pkgconfig"
so() {
	n=$1; shift
	m68k-elf-gcc -shared -Wl,-h,lib$n.so.3 -Wl,--whole-archive lib/lib$n.a -Wl,--no-whole-archive \
		-L"$L" -L"$XL" -Wl,-rpath-link,"$XL" -Wl,-rpath-link,"$L" -Wl,-R,/usr/local/lib:/usr/x11r6/lib "$@" \
		-o "$L/lib$n.so.3"
	ln -sf lib$n.so.3 "$L/lib$n.so"
	cat > "$L/pkgconfig/$(echo $n | tr 'A-Z' 'a-z').pc" <<P
prefix=/usr/local
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $n
Description: Mesa 3.1 lib$n, soft3D on the Z3660
Version: 3.1
Libs: -L\${libdir} -l$n
Cflags: -I\${includedir}
P
	echo "$n: $(wc -c < "$L/lib$n.so.3") bytes"
}
so GL -lXext -lX11
so GLU -lGL
so glut -lGLU -lGL -lXmu -lXi -lXext -lX11
