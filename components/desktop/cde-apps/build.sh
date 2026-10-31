#!/bin/sh
# build.sh [app ...] -- the apps with static Motif and X11R6.3, by gcc14-cross-amix:
#   MOTIF_BUILD   Motif's build tree (lib/Xm/Xm.h, lib/Xm/.libs/libXm.a)
#   MOTIF_SRC     its source (lib/Xm/*.h)
#   XP_LIB        a directory with libXp.a
#   AMIX_SYSROOT  default ~/opt/m68k-amix-sysroot
set -e
: "${MOTIF_BUILD:?}" "${MOTIF_SRC:?}" "${XP_LIB:?}"
X=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6/xc/exports
cd "$(dirname "$0")"
for app in ${*:-amixcast ztopcde}; do
	m68k-elf-gcc -O2 -m68060 -fno-strict-aliasing -Wno-implicit-int -Wno-implicit-function-declaration \
	  -Wno-incompatible-pointer-types -Wno-int-conversion -DFUNCPROTO=15 \
	  -I$MOTIF_BUILD/lib -I$MOTIF_SRC/lib -I$X/include -L$MOTIF_BUILD/lib/Xm/.libs -L$XP_LIB -L$X/lib \
	  -o $app/$app $app/$app.c -Wl,-Bstatic -lXm -lXp -lXmu -lXt -lXext -lX11 -lSM -lICE -Wl,-Bdynamic \
	  -lgen -lsocket -lnsl -lm
	ls -l $app/$app
done
