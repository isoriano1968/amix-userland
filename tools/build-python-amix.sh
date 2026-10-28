#!/bin/bash
# Experimental CPython 3.11 port: no installation or target system changes.
set -eu
project=$(pwd)
source=${PYTHON_SOURCE:-$HOME/src/upstream/Python-3.11.18}
build=${PYTHON_BUILD:-$HOME/build/python311-amix}
host_python=${BUILD_PYTHON:-$HOME/build/python311-host/python}
cc=${CC:-$HOME/opt/m68k-amix/bin/m68k-elf-gcc}
pth=${PTH_PREFIX:-$HOME/opt/pth-amix}
case "$("$cc" -dumpversion)" in 14|14.*) ;; *) exit 1;; esac
test "$("$cc" -dumpmachine)" = m68k-elf
archive="$(dirname "$source")/Python-3.11.18.tar.xz"
mkdir -p "$(dirname "$source")"
if test ! -f "$archive"; then
    curl -fL --retry 3 -o "$archive" \
        https://www.python.org/ftp/python/3.11.18/Python-3.11.18.tar.xz
fi
echo "9120e0a653868f6e3698d36cdd99e5a58903ec9469e448f21d98f7b16d8fa290  $archive" | sha256sum -c -
if test ! -f "$source/configure"; then
    mkdir -p "$source"
    tar -xf "$archive" --strip-components=1 -C "$source"
fi
if test ! -x "$host_python"; then
    host_build=$(dirname "$host_python")
    mkdir -p "$host_build"
    (
        cd "$host_build"
        # Native interpreter only generates frozen code for the cross-build.
        CC=gcc AR=ar RANLIB=ranlib CFLAGS= CPPFLAGS= LDFLAGS= LIBS= \
            "$source/configure" --without-ensurepip --disable-shared
        make -j4 python
    )
fi
test "$("$host_python" -E -S --version)" = 'Python 3.11.18'
test -f "$pth/lib/libpthread.a"
python3 tools/patch-python-amix.py "$source"
mkdir -p "$build"
compat="$project/build/python-compat"
mkdir -p "$compat"
"$cc" --version > "$build/amix-compiler.txt"
for name in math wide clock time environ; do
    "$cc" -O2 -m68060 -fno-strict-aliasing -Iports/python/include \
        -c "ports/python/amix-$name.c" -o "$compat/amix-$name.o"
done
cd "$build"
export CC="$cc" AR="$(dirname "$cc")/m68k-elf-ar"
export RANLIB="$(dirname "$cc")/m68k-elf-ranlib"
export READELF="$(dirname "$cc")/m68k-elf-readelf"
export CFLAGS='-O2 -m68060 -fno-strict-aliasing'
export CPPFLAGS="-I$project/ports/python/include -I$pth/include"
export LDFLAGS="-L$pth/lib"
export LIBS="$compat/amix-math.o $compat/amix-wide.o $compat/amix-clock.o $compat/amix-time.o $compat/amix-environ.o -lm -lpthread -ldl -lsocket -lnsl"
# Both device paths were checked on the physical AMIX machine.
export ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no
if test "${PYTHON_SKIP_CONFIGURE:-no}" != yes; then
    "$source/configure" --build="$("$source/config.guess")" --host=m68k-cbm-sysv4 \
    --with-build-python="$host_python" --prefix=/usr/local/gnu \
    --disable-shared --disable-ipv6 --without-ensurepip \
        --without-c-locale-coercion --disable-test-modules
fi
if test "${PYTHON_EXTRA_MODULES:-no}" = yes; then
    zlib=${ZLIB_SOURCE:-$HOME/src/upstream/zlib-1.3.1}
    test -f "$zlib/libz.a"
    cat > Modules/Setup.local <<EOF
*static*
math mathmodule.c
_struct _struct.c
binascii binascii.c
_json _json.c
_random _randommodule.c
_datetime _datetimemodule.c
select selectmodule.c
_md5 md5module.c
_sha1 sha1module.c
_sha256 sha256module.c
_sha512 sha512module.c
_sha3 _sha3/sha3module.c
_blake2 _blake2/blake2module.c _blake2/blake2b_impl.c _blake2/blake2s_impl.c
zlib zlibmodule.c -I$zlib $zlib/libz.a
EOF
fi
if test "${PYTHON_CLEAN:-no}" = yes; then
    make clean
fi
make -j4 python CPPFLAGS="$CPPFLAGS -std=gnu11 -include $project/ports/python/amix-python.h" \
    CFLAGS="$CFLAGS -DTHREAD_STACK_SIZE=1048576" LIBS="$LIBS" \
    LDFLAGS="$LDFLAGS -Wl,-Map=$build/python-amix.map"
echo 'CPython AMIX executable built; not yet hardware acceptance.'
