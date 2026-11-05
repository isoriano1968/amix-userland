#!/bin/sh
# AMIX SDK flags: static libpython3.11.a on shared GNU Pth, OpenSSL, zlib, bzip2, expat
prefix=/usr/local
include=$prefix/include/python3.11
config=$prefix/lib/python3.11/config-3.11
embed=no
for option in "$@"; do
    case "$option" in --embed) embed=yes;; esac
done
libs='-ldl -lm'
if test "$embed" = yes; then
    libs='-lpython3.11 -lmpdec -lamixpythoncompat -lssl -lcrypto -lbz2 -lexpat -lz -lpthread -lpth -ldl -lsocket -lnsl -lm'
fi
for option in "$@"; do
    case "$option" in
    --prefix|--exec-prefix) echo "$prefix";;
    --includes) echo "-I$include -I$include/amixcompat -I/usr/local/gnu/include";;
    --cflags) echo "-I$include -I$include/amixcompat -I/usr/local/gnu/include -include $include/amix-python.h -O2 -m68060 -std=gnu11 -fno-strict-aliasing";;
    --libs) echo "$libs";;
    --ldflags) echo "-L$prefix/lib -L$config -L/usr/local/gnu/lib $libs";;
    --configdir) echo "$config";;
    --extension-suffix) echo '.cpython-311.so';;
    --abiflags) echo '';;
    --embed) ;;
    --help) echo 'Options: --prefix --exec-prefix --includes --cflags --libs --ldflags --configdir --extension-suffix --abiflags --embed';;
    *) echo "Unknown option: $option" >&2; exit 1;;
    esac
done
