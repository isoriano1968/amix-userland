#!/bin/sh

set -e

TOP=${1-/usr/x11r6/xc}
MODE=${2-all}
LIBRARIES="X11 Xext ICE SM Xt Xmu Xaw"
PROGRAMS="xclock xterm twm"

cd "$TOP"

if test -f config/cf/amix.cf
then
    :
else
    echo "amix-shared-rebuild: $TOP is not an X11R6 source tree" >&2
    exit 1
fi

for setting in \
    'HasSharedLibraries YES' \
    'SeparateSharedCompile YES' \
    'PositionIndependentCFlags -fpic' \
    'SharedLibraryLoadFlags -G' \
    'HasShm NO'
do
    if grep "^#define $setting" config/cf/amix.cf >/dev/null
    then
        :
    else
        echo "amix-shared-rebuild: missing setting: $setting" >&2
        exit 1
    fi
done

if test "x$LD_LIBRARY_PATH" = x
then
    LD_LIBRARY_PATH=$TOP/exports/lib
else
    LD_LIBRARY_PATH=$TOP/exports/lib:$LD_LIBRARY_PATH
fi
export LD_LIBRARY_PATH

BUILD_LIBRARIES=no

if test "$MODE" = all
then
    echo "=== regenerate X11R6 Makefiles ==="
    rm -f xmakefile
    make Makefile
    test -s xmakefile
    make -f xmakefile Makefiles
    BUILD_LIBRARIES=yes
else
    if test "$MODE" = libs
    then
        BUILD_LIBRARIES=yes
    else
        if test "$MODE" = clients
        then
            :
        else
            echo "usage: $0 [source-tree [all|libs|clients]]" >&2
            exit 1
        fi
    fi
fi

if test "$BUILD_LIBRARIES" = yes
then
    for library in $LIBRARIES
    do
        dir="$TOP/lib/$library"
        echo "=== clean PIC rebuild of shared lib$library ==="
        cd "$dir"
        rm -f lib$library.so lib$library.so.* *.o shared/*.o unshared/*.o
        make
    done
fi

echo "=== validate shared libraries ==="
for library in $LIBRARIES
do
    cd "$TOP/lib/$library"
    found=no
    for image in lib$library.so.*
    do
        if test -f "$image" && test -s "$image"
        then
            file "$image"
            dump -Lv "$image" >/dev/null
            found=yes
        fi
    done
    if test "$found" = no
    then
        echo "amix-shared-rebuild: no valid lib$library shared image" >&2
        exit 1
    fi
done

echo "=== rebuild dynamic clients ==="
for program in $PROGRAMS
do
    cd "$TOP/programs/$program"
    rm -f "$program"
    make "$program"
    test -s "$program"
    ldd "$program"
done

echo "AMIX X11R6 shared libraries and clients are ready."
