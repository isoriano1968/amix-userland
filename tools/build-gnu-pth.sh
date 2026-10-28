#!/bin/bash
# GNU Pth is the implementation. The custom poc runtime is never linked here.
set -eu
project=$(pwd)
cc="${CC:-$HOME/opt/m68k-amix/bin/m68k-elf-gcc}"
prefix="${PTH_PREFIX:-$HOME/opt/pth-amix}"
source="${PTH_SOURCE:-$HOME/src/upstream/pth-2.0.7}"
archive="$(dirname "$source")/pth-2.0.7.tar.gz"
case "$("$cc" -dumpversion)" in 14|14.*) ;; *) echo 'GCC 14 required' >&2; exit 1;; esac
mkdir -p "$(dirname "$source")"
if test ! -f "$archive"; then
    curl -fL --retry 3 -o "$archive" https://ftp.gnu.org/gnu/pth/pth-2.0.7.tar.gz
fi
echo "72353660c5a2caafd601b20e12e75d865fd88f6cf1a088b306a3963f0bc77232  $archive" |
    sha256sum -c -
if test ! -f "$source/configure"; then
    tar -xf "$archive" -C "$(dirname "$source")"
fi
cd "$source"
CC="$cc" AR="$(dirname "$cc")/m68k-elf-ar" \
    RANLIB="$(dirname "$cc")/m68k-elf-ranlib" \
    CFLAGS='-O2 -m68060 -std=gnu89 -fno-strict-aliasing' \
    ./configure --host=m68k-cbm-sysv4 --build="$(./config.guess)" \
        --prefix="$prefix" --enable-pthread --disable-shared --enable-static \
        --with-mctx-mth=mcsc --with-mctx-dsp=sc --with-mctx-stk=mc
# Old generated-header dependencies are incomplete: use an ordered clean build
# so the errno macro change reaches every scheduler/caller object.
make clean
python3 "$project/tools/patch-gnu-pth.py" "$source"
make pth_p.h
make -j4
make install
echo "GNU Pth pthread headers/libraries installed privately in $prefix"
