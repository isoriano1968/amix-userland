#!/bin/sh
# build.sh -- vplay for AMIX (68060), cross-built with gcc14-cross-amix
# (m68k-elf-gcc on PATH) against build-ff/'s FFmpeg 4.4 (configure-amix.sh,
# make), X11R6.3's exports and the static MIT-SHM libraries:
#   AMIX_SYSROOT  default ~/opt/m68k-amix-sysroot
#   SHM_LIBS      directory with libXext.a (with XShm) and libshmsys.a
set -e
V=$(cd "$(dirname "$0")" && pwd); B=$V/build-ff
X=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6/xc/exports
[ -n "$SHM_LIBS" ] && [ -f "$SHM_LIBS/libXext.a" ] && [ -f "$SHM_LIBS/libshmsys.a" ] || {
	echo "SHM_LIBS must name a directory with libXext.a (with XShm) and libshmsys.a"; exit 1; }
m68k-elf-gcc -O2 -m68060 -fno-strict-aliasing -Wall -Wno-deprecated-declarations \
  -Wno-error=incompatible-pointer-types -Wno-implicit-int -I$V/compat/include \
  -DVPLAY_S3D -DVPLAY_ZZMPEG -I$V/ffmpeg-4.4.8 -I$B -I$X/include -o $V/vplay $V/src/vplay.c $V/src/libs3d/s3d.c \
  $V/src/zzmpeg.c $V/src/zzav.c \
  $B/libavformat/libavformat.a $B/libavcodec/libavcodec.a $B/libswresample/libswresample.a \
  $B/libswscale/libswscale.a $B/libavutil/libavutil.a \
  -L"$SHM_LIBS" -L$X/lib -L$V/compat -lXext -lX11 -lshmsys -lvplaycompat -lsocket -lnsl -lm
ls -l $V/vplay
