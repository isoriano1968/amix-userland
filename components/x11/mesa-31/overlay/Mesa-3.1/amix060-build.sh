#!/bin/sh
# amix060-build.sh [demos] -- Mesa 3.1 for the 68060, cross-built on a Linux
# host with gcc14-cross-amix (Make-config: amix060): static libGL, libGLU and
# libglut in lib/, with the Z3660 soft3D driver and MIT-SHM.  "demos" also
# builds the demos (demos/build060.sh).  m68k-elf-gcc must be on PATH;
# AMIX_SYSROOT names the sysroot (default ~/opt/m68k-amix-sysroot).
set -e
cd "$(dirname "$0")"
mkdir -p lib
for d in src src-glu src-glut; do
	echo "== $d"
	(cd $d && make -f Makefile.X11 amix060 ${AMIX_SYSROOT:+AMIX_SYSROOT=$AMIX_SYSROOT}) > $d-amix060.log 2>&1 || { echo "failed: $d/../$d-amix060.log"; exit 1; }
done
ls -l lib
[ "$1" = demos ] && sh demos/build060.sh
exit 0
