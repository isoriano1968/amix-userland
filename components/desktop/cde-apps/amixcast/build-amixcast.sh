#!/bin/sh
# build-amixcast.sh -- the CDE app, with static Motif (build-motif-amix) and X11R6.3
set -e
R=${USR_AMIX:-$HOME/Sources/usr-amix}
X=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6/xc/exports
cd "$(dirname "$0")"
m68k-elf-gcc -O2 -m68060 -fno-strict-aliasing -Wno-implicit-int -Wno-implicit-function-declaration \
  -Wno-incompatible-pointer-types -Wno-int-conversion -DFUNCPROTO=15 \
  -I$R/build-motif-amix/lib -I$R/src-motif/motif-2.3.8/lib -I$X/include -L$R/build-motif-amix/lib/Xm/.libs -L$R/x11r63-amix/build-libXp -L$X/lib \
  -o amixcast amixcast.c -Wl,-Bstatic -lXm -lXp -lXmu -lXt -lXext -lX11 -lSM -lICE -Wl,-Bdynamic -lgen -lsocket -lnsl -lm
ls -l amixcast
