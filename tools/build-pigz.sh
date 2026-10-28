#!/bin/bash
# Initial regular-file application proof, with upstream sources kept outside Git.
set -eu
project=$(pwd)
cc="$HOME/opt/m68k-amix/bin/m68k-elf-gcc"
ar="$HOME/opt/m68k-amix/bin/m68k-elf-ar"
zlib="$HOME/src/upstream/zlib-1.3.1"
pigz="$HOME/src/upstream/pigz-2.8"
test -x "$cc"
test -f "$zlib/configure"
test -f "$pigz/pigz.c"
mkdir -p "$project/build"
cd "$zlib"
CC="$cc" AR="$ar" RANLIB="$HOME/opt/m68k-amix/bin/m68k-elf-ranlib" \
    CFLAGS='-O2 -m68060 -std=gnu89 -fno-strict-aliasing' \
    ./configure --static
make -j4 libz.a
cd "$project"
"$cc" -O2 -m68060 -std=gnu99 -fno-strict-aliasing -DNOZOPFLI \
    -DAMIX_PTHREAD_TRACE \
    -Dva_copy=__builtin_va_copy -include ports/pigz/amix-port.h \
    -Ipoc -Iports/pigz/include -I"$zlib" \
    "$pigz/pigz.c" "$pigz/yarn.c" "$pigz/try.c" poc/pthread.c \
    ports/pigz/amix-port.c "$zlib/libz.a" -lm -o build/pigz-proof
echo 'Built experimental pigz with the cooperative runtime; hardware test required.'
