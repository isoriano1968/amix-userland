#!/bin/sh
# GNU Pth application acceptance. Never add poc/pthread.c to these link lines.
set -eu
CC="${CC:-$HOME/opt/m68k-amix/bin/m68k-elf-gcc}"
PTH_PREFIX="${PTH_PREFIX:-$HOME/opt/pth-amix}"
case "$("$CC" -dumpversion)" in 14|14.*) ;; *) echo 'GCC 14 required' >&2; exit 1;; esac
test -f "$PTH_PREFIX/lib/libpthread.a"
mkdir -p build
"$CC" --version > build/compiler.txt
echo '-O2 -m68060 -std=gnu89 -fno-strict-aliasing; GNU Pth libpthread.a' > build/flags.txt
for name in context-roundtrip context-stack context-state bounded-run; do
    "$CC" -O2 -m68060 -std=gnu89 -fno-strict-aliasing \
        "probes/$name.c" -o "build/$name"
done
"$CC" -O2 -m68060 -std=gnu89 -fno-strict-aliasing -DAMIX_USE_GNU_PTH \
    -I"$PTH_PREFIX/include" poc/proof.c -L"$PTH_PREFIX/lib" \
    -lpthread -ldl -lsocket -lnsl -Wl,-Map=build/gnu-pthread-proof.map \
    -o build/gnu-pthread-proof
"$CC" -O2 -m68060 -std=gnu89 -fno-strict-aliasing \
    -I"$PTH_PREFIX/include" tests/gnu-pth-io.c -L"$PTH_PREFIX/lib" \
    -lpthread -ldl -lsocket -lnsl -o build/gnu-pth-io
echo 'Built GNU Pth acceptance binaries; run instruction checks before deployment.'
"$CC" -O2 -m68060 -std=gnu89 -fno-strict-aliasing \
    -I"$PTH_PREFIX/include" tests/gnu-pth-once.c -L"$PTH_PREFIX/lib" \
    -lpthread -ldl -lsocket -lnsl -o build/gnu-pth-once
