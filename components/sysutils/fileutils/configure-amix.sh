#!/bin/sh
# configure-amix.sh -- fileutils 4.1 for AMIX with gcc14-cross-amix (static
# answers for the run-time tests in amix.cache; --disable-nls)
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=return-mismatch -Wno-error=implicit-int"
cd "$H/fileutils-4.1"
cp "$H/amix.cache" config.cache
CC=m68k-elf-gcc CFLAGS="-O2 -I$H/amix-include -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fcommon $W" \
	./configure --cache-file=config.cache --host=m68k-elf --build=i686-pc-linux-gnu \
	--prefix=/usr/local --disable-nls
