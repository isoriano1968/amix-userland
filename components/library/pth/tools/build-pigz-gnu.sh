#!/bin/bash
# Link the application against GNU Pth's pthread library, never poc/pthread.c.
set -eu
cc="$HOME/opt/m68k-amix/bin/m68k-elf-gcc"
prefix="$HOME/opt/pth-amix"
zlib="$HOME/src/upstream/zlib-1.3.1"
pigz="$HOME/src/upstream/pigz-2.8"
test -f "$prefix/lib/libpthread.a"
test -f "$zlib/libz.a"
mkdir -p build
"$cc" -O2 -m68060 -std=gnu99 -fno-strict-aliasing -DNOZOPFLI \
    -Dva_copy=__builtin_va_copy -include ports/pigz/amix-port.h \
    -I"$prefix/include" -Iports/pigz/include -I"$zlib" \
    "$pigz/pigz.c" "$pigz/yarn.c" "$pigz/try.c" ports/pigz/amix-port.c \
    "$zlib/libz.a" -L"$prefix/lib" -lpthread -ldl -lsocket -lnsl -lm \
    -Wl,-Map=build/pigz-gnu.map -o build/pigz-gnu
echo 'Built pigz against GNU Pth libpthread.a; no custom runtime linked.'
