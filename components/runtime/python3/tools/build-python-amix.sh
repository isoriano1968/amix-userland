#!/bin/bash
# CPython 3.11 for AMIX: pristine upstream archive, the AMIX script edits of
# tools/patch-python-amix.py, then patches/*.patch; cross-built with GCC 14.
# Libraries come from the AMIX sysroot through tools/amix-pkg-config: GNU Pth,
# zlib, bzip2, expat, ncurses and OpenSSL, all shared, found at run time
# through the runpath /usr/local/lib:/usr/local/gnu/lib.  Every stdlib
# extension module is linked into the interpreter (Modules/Setup.stdlib,
# static), so lib-dynload stays empty.  No installation or target changes.
set -eu
project=$(pwd)
top=$(cd "$project/../../.." && pwd)
upstream=${PYTHON_UPSTREAM:-$HOME/src/upstream}
source=${PYTHON_SOURCE:-$upstream/Python-3.11.18}
build=${PYTHON_BUILD:-$HOME/build/python311-amix}
host_python=${BUILD_PYTHON:-$HOME/build/python311-host/python}
cc=${CC:-$HOME/opt/m68k-amix/bin/m68k-elf-gcc}
sysroot=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}
case "$("$cc" -dumpversion)" in 14|14.*) ;; *) exit 1;; esac
test "$("$cc" -dumpmachine)" = m68k-elf
archive="$upstream/Python-3.11.18.tar.xz"
mkdir -p "$upstream"
if test ! -f "$archive"; then
    curl -fL --retry 3 -o "$archive.part" \
        https://www.python.org/ftp/python/3.11.18/Python-3.11.18.tar.xz
    mv "$archive.part" "$archive"
fi
echo "9120e0a653868f6e3698d36cdd99e5a58903ec9469e448f21d98f7b16d8fa290  $archive" | sha256sum -c -
if test ! -x "$host_python"; then
    host_build=$(dirname "$host_python")
    hsrc="$host_build/src"
    rm -rf "$hsrc"; mkdir -p "$hsrc"
    tar -xf "$archive" --strip-components=1 -C "$hsrc"
    (
        cd "$host_build"
        # Native interpreter only generates frozen code for the cross-build.
        CC=gcc AR=ar RANLIB=ranlib CFLAGS= CPPFLAGS= LDFLAGS= LIBS= \
            "$hsrc/configure" --without-ensurepip --disable-shared
        make -j8 python
    )
fi
test "$("$host_python" -E -S --version)" = 'Python 3.11.18'
if test "${PYTHON_SKIP_CONFIGURE:-no}" != yes; then
    # always a fresh, pristine tree: the patches apply to nothing else
    rm -rf "$source"; mkdir -p "$source"
    tar -xf "$archive" --strip-components=1 -C "$source"
    python3 tools/patch-python-amix.py "$source"
    for p in "$project"/patches/*.patch; do
        patch -d "$source" -p1 --no-backup-if-mismatch < "$p"
    done
fi
compat="$project/build/python-compat"
mkdir -p "$compat" "$build"
"$cc" --version > "$build/amix-compiler.txt"
for name in math wide clock time environ inet connect; do
    "$cc" -O2 -m68060 -fno-strict-aliasing -Iports/python/include \
        -c "ports/python/amix-$name.c" -o "$compat/amix-$name.o"
done
# an archive, not objects in LIBS: configure prepends -l tests to LIBS and
# the objects would then be linked twice
rm -f "$compat/libamixpythoncompat.a"
"$(dirname "$cc")/m68k-elf-ar" rc "$compat/libamixpythoncompat.a" "$compat"/amix-*.o
cd "$build"
tools="$(dirname "$cc")"
export CC="$cc" AR="$tools/m68k-elf-ar" RANLIB="$tools/m68k-elf-ranlib"
export READELF="$tools/m68k-elf-readelf" PKG_CONFIG="$top/tools/amix-pkg-config"
export AMIX_SYSROOT="$sysroot"
export CFLAGS='-O2 -m68060 -fno-strict-aliasing'
export CPPFLAGS="-I$project/ports/python/include -I$sysroot/usr/local/gnu/include -I$sysroot/usr/local/include"
export LDFLAGS="-L$compat -L$sysroot/usr/local/gnu/lib -L$sysroot/usr/local/lib -Wl,-R,/usr/local/lib:/usr/local/gnu/lib"
export LIBS="-lamixpythoncompat -lm -lpthread -lpth -ldl -lsocket -lnsl"
# Both device paths were checked on the physical AMIX machine.
export ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no
# libsocket has these, but configure's tests compile against the AMIX headers,
# which declare none of them; without the answers _socket has no socket().
# inet_pton/inet_ntop are ports/python/amix-inet.c (IPv4).
for f in socket socketpair accept bind connect listen getsockname getpeername \
         recvfrom sendto setsockopt inet_pton; do
    export ac_cv_func_$f=yes
done
# all modules into the interpreter; no liblzma, libuuid or sqlite for AMIX yet
export MODULE_BUILDTYPE=static
export py_cv_module__lzma=n/a py_cv_module__uuid=n/a
export py_cv_module_xxlimited=n/a py_cv_module_xxlimited_35=n/a
# the interpreter exports the Python C API, so that extension modules
# (pygame, ...) dlopened from lib-dynload or site-packages can link to it;
# configure keeps a preset LINKFORSHARED, and sysconfig reports it
export LINKFORSHARED='-Wl,-E'
if test "${PYTHON_SKIP_CONFIGURE:-no}" != yes; then
    "$source/configure" --build="$("$source/config.guess")" --host=m68k-cbm-sysv4 \
        --with-build-python="$host_python" --prefix=/usr/local \
        --disable-shared --disable-ipv6 --without-ensurepip \
        --without-c-locale-coercion --disable-test-modules \
        --with-openssl="$sysroot/usr/local" --with-system-expat \
        > configure.log 2>&1 || { tail -30 configure.log; exit 1; }
fi
if test "${PYTHON_CLEAN:-no}" = yes; then
    make clean
fi
make -j8 python CPPFLAGS="$CPPFLAGS -std=gnu11 -include $project/ports/python/amix-python.h" \
    CFLAGS="$CFLAGS -DTHREAD_STACK_SIZE=1048576" \
    LDFLAGS="$LDFLAGS -Wl,-Map=$build/python-amix.map" > make.log 2>&1 ||
    { grep -E 'error:|undefined reference' make.log | head -30; exit 1; }
# ld.so.1 reads every object's NEEDED names through the program's strtab
# (libnsl dlopens for its name lookups): the names must come first
"$top/tools/amix-fix-needed" python
"$tools/m68k-elf-readelf" -d python | grep -E 'NEEDED|RPATH'
echo 'CPython AMIX executable built; not yet hardware acceptance.'
