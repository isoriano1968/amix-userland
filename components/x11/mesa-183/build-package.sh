#!/bin/sh
# build-package.sh STAGE -- Mesa 18 as the system's GL in STAGE/usr/local: libGL.so.18
# from amix-gate (core1/build/libgl-amix.sh), the headers and the .pc files.
# GLU and GLUT are library/glu and library/freeglut.
set -e
STAGE=$1
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
GATE=$H/../../../../amix-gate/usr/src/firmware/core1
L=$STAGE/usr/local/lib
I=$STAGE/usr/local/include
XL=$STAGE/usr/x11r6/lib
rm -f "$L"/libGL.so "$L"/libGL.so.3 "$L"/libGLU.so "$L"/libGLU.so.3 "$L"/libglut.so "$L"/libglut.so.3
cp "$GATE/build/amix-glx/libGL.so.18" "$L/"
ln -sf libGL.so.18 "$L/libGL.so"
M=$GATE/src/mesa-18.3.6/include
rm -rf "$I/GL" "$I/KHR"
mkdir -p "$I/GL" "$I/KHR"
cp "$M/GL/gl.h" "$M/GL/glext.h" "$M/GL/glcorearb.h" "$M/GL/glx.h" "$M/GL/glxext.h" "$I/GL/"
cp "$M/KHR/khrplatform.h" "$I/KHR/"
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
ls -l "$L"/libGL* "$L"/libglut*
