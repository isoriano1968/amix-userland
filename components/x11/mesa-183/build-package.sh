#!/bin/sh
# build-package.sh STAGE -- Mesa 18 as the system's GL in STAGE/usr/local: libGL.so.18
# from amix-gate (core1/build/libgl-amix.sh), libGLU and libglut on it, the
# headers and the .pc files.  GLU and GLUT are still Mesa 3.1's sources, from
# x11/mesa-31's work-pic; the sonames are new, so nothing mixes with Mesa 3.
set -e
STAGE=$1
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
GATE=$H/../../../../amix-gate/usr/src/firmware/core1
W=$H/../mesa-31/work-pic
L=$STAGE/usr/local/lib
I=$STAGE/usr/local/include
XL=$STAGE/usr/x11r6/lib
rm -f "$L"/libGL.so "$L"/libGL.so.3 "$L"/libGLU.so "$L"/libGLU.so.3 "$L"/libglut.so "$L"/libglut.so.3
cp "$GATE/build/amix-glx/libGL.so.18" "$L/"
ln -sf libGL.so.18 "$L/libGL.so"
so() {
	n=$1; shift
	m68k-elf-gcc -shared -Wl,-h,lib$n.so.18 -Wl,--whole-archive "$W/lib/lib$n.a" -Wl,--no-whole-archive \
		-L"$L" -L"$XL" -Wl,-rpath-link,"$XL" -Wl,-rpath-link,"$L" -Wl,-R,/usr/local/lib:/usr/x11r6/lib "$@" \
		-o "$L/lib$n.so.18" 2>&1 | grep -v 'libc.so.1' || true
	m68k-elf-strip "$L/lib$n.so.18"
	"$H/../../../tools/amix-fix-data-relocs" "$L/lib$n.so.18"
	ln -sf lib$n.so.18 "$L/lib$n.so"
}
so GLU -lGL
so glut -lGLU -lGL -lXmu -lXi -lXext -lX11
M=$GATE/src/mesa-18.3.6/include
rm -rf "$I/GL" "$I/KHR"
mkdir -p "$I/GL" "$I/KHR"
cp "$M/GL/gl.h" "$M/GL/glext.h" "$M/GL/glcorearb.h" "$M/GL/glx.h" "$M/GL/glxext.h" "$I/GL/"
cp "$M/KHR/khrplatform.h" "$I/KHR/"
cp "$W/include/GL/glu.h" "$W/include/GL/glut.h" "$I/GL/"
pc() {
	cat > "$L/pkgconfig/$1.pc" <<P
prefix=/usr/local
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $2
Description: $3
Version: 18.3.6
Requires: $4
Libs: -L\${libdir} -l$2
Cflags: -I\${includedir}
P
}
pc gl GL "Mesa 18 libGL, GLX, Gallium on the Z3660" "xext x11"
pc glu GLU "GLU on Mesa 18" "gl"
pc glut glut "GLUT on Mesa 18" "glu gl xmu xi"
ls -l "$L"/libGL* "$L"/libglut*
