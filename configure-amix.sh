#!/bin/sh
# configure-amix.sh -- configure FFmpeg 4.4 for AMIX (68060) in build-ff/,
# cross-built with gcc14-cross-amix (m68k-elf-gcc on PATH).
#   sh configure-amix.sh          -> build-ff/      ffmpeg, and the libraries vplay links
#   sh configure-amix.sh ffplay   -> build-ffplay/  ffmpeg and ffplay, on SDL 2 (sdl2-amix),
#                                    with patches/ applied to ffplay
# The ffplay build compiles every file with gnu-pth's pthread.h, so read(),
# usleep() and select() let ffplay's other threads run.  SDL2_AMIX names the
# sdl2-amix tree (default ../sdl2-amix).
set -e
V=$(cd "$(dirname "$0")" && pwd)
C=$V/compat
SR=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}
BD=build-ff
EXTRA_CF=
EXTRA_LIBS=
PLAY="--disable-ffplay"
if [ "$1" = ffplay ]; then
	BD=build-ffplay
	S2=${SDL2_AMIX:-$V/../sdl2-amix}
	[ -x "$S2/build/sdl2-config-cross" ] || { echo "no $S2/build/sdl2-config-cross: build sdl2-amix first"; exit 1; }
	mkdir -p "$V/$BD/bin"
	ln -sf "$S2/build/sdl2-config-cross" "$V/$BD/bin/sdl2-config"
	PATH=$V/$BD/bin:$PATH
	EXTRA_CF="-I$SR/usr/local/gnu/include -include $SR/usr/local/gnu/include/pthread.h"
	# linked too, or configure finds no usleep (pthread.h renames it) and
	# av_usleep never sleeps: ffplay's loop would starve its other threads
	# PTH_LIB: another gnu-pth build, e.g. one with the sjlj thread switch
	EXTRA_LIBS="-L${PTH_LIB:-$SR/usr/local/gnu/lib} -lpthread -lpth -lsocket -lnsl"
	PLAY="--enable-ffplay --enable-sdl2 --pkg-config=$V/tools/pkg-config-sdl2"
	# patches/: ffplay's own changes for AMIX (applied once)
	for p in "$V"/patches/*.patch; do
		patch -s -N --dry-run -p1 -d "$V/ffmpeg-4.4.8" < "$p" > /dev/null 2>&1 &&
			patch -s -p1 -d "$V/ffmpeg-4.4.8" < "$p"
	done
fi
mkdir -p "$V/$BD"
cd "$V/$BD"
"$V/ffmpeg-4.4.8/configure" --prefix=/usr/local \
 --enable-cross-compile --arch=m68k --cpu=68060 --target-os=none \
 --cc=m68k-elf-gcc --ar=m68k-elf-ar --ranlib=m68k-elf-ranlib --nm=m68k-elf-nm --strip=m68k-elf-strip \
 $( [ -n "$S2" ] || echo --pkg-config=false ) \
 --extra-cflags="-m68060 -fno-strict-aliasing -D_XOPEN_SOURCE=600 $EXTRA_CF -I$C/include -include $C/include/amix-ff.h -Wno-error=incompatible-pointer-types" \
 --extra-ldflags="-L$C" --extra-libs="-lvplaycompat $EXTRA_LIBS -lsocket -lnsl -lm" \
 --disable-autodetect --disable-asm --disable-pthreads --disable-doc --disable-debug \
 $PLAY --disable-ffprobe --enable-static --disable-shared \
 --disable-everything \
 --enable-decoder=h264,mpeg4,msmpeg4v3,mpeg1video,mpeg2video,mjpeg,aac,mp3float,mp2float,ac3,pcm_s16le \
 --enable-demuxer=mov,matroska,avi,mpegps,mpegts,mpegvideo,h264,m4v,mp3,aac,wav \
 --enable-parser=h264,mpeg4video,mpegvideo,aac,mpegaudio,ac3 \
 --enable-protocol=file,pipe,tcp,http \
 --enable-muxer=null,rawvideo,wav \
 --enable-encoder=wrapped_avframe,rawvideo,pcm_s16le \
 --enable-filter=null,anull,format,aformat,aresample,scale,copy,fps,setpts,asetpts \
 --enable-bsf=h264_mp4toannexb,aac_adtstoasc \
 --enable-swscale --enable-swresample
# AMIX's memalign blocks do not survive realloc/free in its malloc (crash
# in realfree); the 68060 build has no SIMD, so plain malloc is enough.
sed -i 's/^#define HAVE_MEMALIGN 1$/#define HAVE_MEMALIGN 0/' config.h
# The 68060's FPU rounding and sqrt from compat/amix-math.c (AMIX's libm has
# no rint/lrint/round/trunc, and its floor/ceil/sqrt take 2.5-5.8 us each).
for f in RINT LRINT LRINTF LLRINT LLRINTF ROUND ROUNDF TRUNC TRUNCF; do
	sed -i "s/^#define HAVE_$f 0$/#define HAVE_$f 1/" config.h
done
# configure tests functions by calling them without arguments; with the real
# prototypes (amix-ff.h includes sys/time.h and sys/resource.h) those tests
# fail although the functions are there.  Without gettimeofday, av_gettime()
# returns a constant: ffplay's clock stands still.
for f in GETTIMEOFDAY SETRLIMIT; do
	sed -i "s/^#define HAVE_$f 0$/#define HAVE_$f 1/" config.h
done
# With gnu-pth's pthread.h forced in, configure's prototype-less tests of
# these fail although they exist (usleep becomes __pthread_usleep): the same
# as the plain build.  Without usleep, av_usleep would never sleep.
if [ -n "$S2" ]; then
	for f in ACCESS ISATTY SYSCONF USLEEP; do
		sed -i "s/^#define HAVE_$f 0$/#define HAVE_$f 1/" config.h
	done
fi
