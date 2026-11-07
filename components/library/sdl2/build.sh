#!/bin/sh
# build.sh -- SDL 2.30 for AMIX (68040/68060), cross-built with gcc14-cross-amix
# (m68k-elf-gcc on PATH) against X11R6.3 and gnu-pth-amix, as the objects of a
# shared library:
#   sh build.sh   -> build/libSDL2.a (-fPIC; the Makefile makes
#                    libSDL2-2.0.so.0 of it) and build/SDL2-2.30.12 with the
#                    patched source, SDL_config.h, sdl2.pc and sdl2-config
#   AMIX_SYSROOT  default ~/opt/m68k-amix-sysroot (with the shared X11R6.3 in
#                 usr/x11r6 and gnu-pth in usr/local/gnu)
#   AMIX_GATE     amix-gate (default beside amix-userland): core 1's zzgl.h and
#                 bench.h for the zzhdmi audio driver
# The source: SDL2-2.30.12/ (fetch.sh), copied to build/, patches/ applied and
# amix/ laid over it.
set -e
H=$(cd "$(dirname "$0")" && pwd)
V=2.30.12
SR=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}
X=$SR/usr/x11r6
G=$SR/usr/local/gnu
B=$H/build/SDL2-$V
O=$H/build/obj
C1=${AMIX_GATE:-$H/../../../../amix-gate}/usr/src/firmware/core1
[ -d "$H/SDL2-$V" ] || { echo "no SDL2-$V/: run fetch.sh"; exit 1; }
[ -f "$G/include/pthread.h" ] || { echo "no gnu-pth in $G"; exit 1; }
[ -f "$C1/zzgl/zzgl.h" ] || { echo "no amix-gate's core 1 headers in $C1"; exit 1; }

rm -rf "$B" "$O"
mkdir -p "$H/build" "$O"
cp -a "$H/SDL2-$V" "$B"
for p in "$H"/patches/*.patch; do
	patch -s -d "$B" -p1 < "$p"
done
cp -a "$H/amix/src/." "$B/src/"
python3 "$H/amix/mkconfig.py" "$B/include/SDL_config.h.cmake" > "$B/include/SDL_config.h"

# gnu-pth has no POSIX semaphores: SDL's generic ones (mutex + condition).
# every file sees gnu-pth's pthread.h: read, write, select, poll and the sleeps
# become calls that let the other threads run.  Hidden by default, as
# upstream builds it: the library exports SDL's API (DECLSPEC) and no more.
CF="-O2 -m68060 -fPIC -fvisibility=hidden -fno-strict-aliasing -D_REENTRANT -DUSING_GENERATED_CONFIG_H
	-I$B/include -I$B/src -I$G/include -include $G/include/pthread.h -include $H/amix/include/SDL_amix.h
	-I$X/include
	-Wno-implicit-int -Wno-error=incompatible-pointer-types -Wno-deprecated-declarations"
cd "$B/src"
# joystick, haptic and sensor are off (SDL_config.h), but SDL's core still
# calls their dummy drivers: without them the library has undefined symbols.
# hidapi and loadso: their stubs, so that the whole API is there
SRCS=$(ls *.c atomic/*.c audio/*.c audio/dummy/*.c audio/amix/*.c audio/zzhdmi/*.c cpuinfo/*.c \
	events/*.c file/*.c filesystem/unix/*.c haptic/*.c haptic/dummy/*.c joystick/*.c joystick/dummy/*.c libm/*.c \
	hidapi/SDL_hidapi.c loadso/dummy/*.c locale/*.c locale/dummy/*.c misc/*.c misc/dummy/*.c power/*.c render/*.c \
	render/software/*.c render/zz9k/*.c sensor/*.c sensor/dummy/*.c stdlib/*.c thread/*.c \
	thread/pthread/SDL_systhread.c thread/pthread/SDL_sysmutex.c thread/pthread/SDL_syscond.c \
	thread/pthread/SDL_systls.c thread/generic/SDL_syssem.c core/amix/*.c \
	timer/*.c timer/unix/*.c video/*.c video/dummy/*.c video/x11/*.c video/yuv2rgb/*.c \
	core/unix/SDL_poll.c dynapi/*.c 2>/dev/null)
fail=0
for f in $SRCS; do
	o=$O/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')
	case $f in audio/zzhdmi/*) XF="-I$C1/zzgl -I$C1/cores/h264bench" ;; *) XF= ;; esac
	m68k-elf-gcc $CF $XF -c "$f" -o "$o" 2> "$o.log" || { echo "FAIL $f"; sed -n 1,12p "$o.log"; fail=1; }
done
[ $fail = 0 ] || exit 1
rm -f "$H/build/libSDL2.a"
m68k-elf-ar rc "$H/build/libSDL2.a" "$O"/*.o
# sdl2.pc and sdl2-config for AMIX's paths, the shared library.  It names X,
# Pth, libsocket and libnsl itself (NEEDED), but sdl2.pc requires x11 and pth
# too: libsocket's own needs (syslog, seteuid) come from libc's archive only
# when the program names -lsocket (x11.pc), and the program's runpath then
# has /usr/x11r6/lib and /usr/local/gnu/lib.  No runpath in either file:
# AMIX's ld has no -R (LD_RUN_PATH on the box; amix-pkg-config adds -R for
# cross builds).
sub="s|@prefix@|/usr/local|; s|@exec_prefix@|\${prefix}|; s|@bindir@|\${exec_prefix}/bin|;
	s|@libdir@|\${exec_prefix}/lib|; s|@includedir@|\${prefix}/include|; s|@SDL_VERSION@|$V|;
	s|@bin_prefix_relpath@|..|; s|@SDL_CFLAGS@|-D_REENTRANT|; s|@SDL_LIBS@|-lSDL2|;
	s| *@SDL_RLD_FLAGS@||; s| *@PKGCONFIG_LIBS_PRIV@||; s| *@SDL_STATIC_LIBS@||;
	s|@PKGCONFIG_REQUIRES@|x11 pth|; s|@PKGCONFIG_DEPENDS@||;
	s|@ENABLE_STATIC_TRUE@|#|; s|@ENABLE_STATIC_FALSE@||; s|@ENABLE_SHARED_TRUE@||; s|@ENABLE_SHARED_FALSE@|#|"
sed -e "$sub" "$B/sdl2.pc.in" > "$B/sdl2.pc"
sed -e "$sub" "$B/sdl2-config.in" > "$B/sdl2-config"
ls -l "$H/build/libSDL2.a"
