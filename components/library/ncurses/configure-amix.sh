#!/bin/sh
# configure-amix.sh -- ncurses 6.6 for AMIX (SVR4.0, m68k) with gcc14-cross-amix.
# Narrow characters (AMIX has no wchar support).  ncurses' own shared rules do
# not know this target, so everything is compiled -fPIC and the Makefile makes
# the shared libraries from the archives.  One terminfo
# database: AMIX's own /usr/share/lib/terminfo first (where AMIX's tic writes),
# then /usr/share/terminfo (the 2005 ncurses one vim 5.8 uses).  The programs
# get an "n" prefix so they do not shadow AMIX's tic/tput in PATH.
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=return-mismatch -Wno-error=implicit-int"
mkdir -p "$H/build" && cd "$H/build"
CC=m68k-elf-gcc CXX=m68k-elf-g++ AR=m68k-elf-ar RANLIB=m68k-elf-ranlib \
CFLAGS="-O2 -fPIC -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fcommon $W" \
BUILD_CC=gcc BUILD_CFLAGS=-O2 \
	"$H/ncurses-6.6/configure" --host=m68k-elf --build=x86_64-pc-linux-gnu \
	--prefix=/usr/local --program-prefix=n \
	--with-default-terminfo-dir=/usr/share/lib/terminfo \
	--with-terminfo-dirs=/usr/share/lib/terminfo:/usr/share/terminfo \
	--without-shared --with-normal --without-debug --without-ada \
	--without-cxx-binding --without-tests --without-manpages \
	--disable-widec --disable-big-core --enable-overwrite \
	--with-fallbacks= --disable-db-install
