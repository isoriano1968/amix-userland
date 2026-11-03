#!/bin/sh
# configure-amix.sh -- pkgconf for AMIX with gcc14-cross-amix.  It looks in
# the same places as tools/amix-pkg-config does in the sysroot.
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types"
mkdir -p "$H/build" && cd "$H/build"
CC=m68k-elf-gcc AR=m68k-elf-ar RANLIB=m68k-elf-ranlib \
CFLAGS="-I$H/compat -include $H/compat/amix.h -O2 -m68060 -D__amix__ -D__svr4__ -DSVR4 -fcommon $W" \
	"$H/pkgconf-2.3.0/configure" --host=m68k-elf --build=x86_64-pc-linux-gnu \
	--prefix=/usr/local --disable-shared --enable-static \
	--with-pkg-config-dir=/usr/local/lib/pkgconfig:/usr/local/gnu/lib/pkgconfig:/usr/gnu/lib/pkgconfig:/usr/dt/lib/pkgconfig:/usr/motif/lib/pkgconfig:/usr/x11r6/lib/pkgconfig:/usr/X/lib/pkgconfig:/usr/lib/pkgconfig \
	--with-system-libdir=/usr/lib --with-system-includedir=/usr/include
