#!/bin/sh

set -e

TOP=${1-/usr/x11r6/xc}
MODE=${2-all}
LIBRARIES="X11 Xext ICE SM Xt Xmu Xaw"
PROGRAMS="xinit xsetroot xclock xterm twm"

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
    'SharedDataSeparation YES' \
    'SharedCodeDef -DSHAREDCODE' \
    'SharedLibraryDef -DSUNSHLIB' \
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
BUILD_SET=

if test "$MODE" = all
then
    echo "=== regenerate X11R6 Makefiles ==="
    rm -f xmakefile
    make Makefile
    test -s xmakefile
    make -f xmakefile Makefiles
    BUILD_LIBRARIES=yes
    BUILD_SET=$LIBRARIES
else
    if test "$MODE" = shdata
    then
        echo "=== regenerate Makefiles for shared-data ABI ==="
        rm -f xmakefile
        make Makefile
        test -s xmakefile
        make -f xmakefile Makefiles
        BUILD_LIBRARIES=yes
        BUILD_SET="Xt Xmu Xaw"
    else
        if test "$MODE" = shdata-fix
        then
            echo "=== regenerate Makefiles for Xt/Xaw shared-data objects ==="
            rm -f xmakefile
            make Makefile
            test -s xmakefile
            make -f xmakefile Makefiles
            BUILD_LIBRARIES=yes
            BUILD_SET="Xt Xaw"
        else
        if test "$MODE" = libs
        then
            BUILD_LIBRARIES=yes
            BUILD_SET=$LIBRARIES
        else
            if test "$MODE" = clients
            then
                :
            else
                echo "usage: $0 [source-tree [all|shdata|shdata-fix|libs|clients]]" >&2
                exit 1
            fi
        fi
        fi
    fi
fi

if test "$BUILD_LIBRARIES" = yes
then
    if echo " $BUILD_SET " | grep ' Xt ' >/dev/null
    then
        grep 'UNSHAREDOBJS.*unshared/StringDefs.o.*sharedlib.o' \
            "$TOP/lib/Xt/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xt Makefile does not use the normal StringDefs object" >&2
            exit 1
        }
        grep '^unshared/StringDefs.o.*StringDefs.o' \
            "$TOP/lib/Xt/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xt normal-object dependency is missing" >&2
            exit 1
        }
    fi

    if echo " $BUILD_SET " | grep ' Xaw ' >/dev/null
    then
        grep 'UNSHAREDOBJS.*unshared/AllWidgets.o.*sharedlib.o' \
            "$TOP/lib/Xaw/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xaw Makefile does not use the normal AllWidgets object" >&2
            exit 1
        }
        grep '^unshared/AllWidgets.o.*AllWidgets.o' \
            "$TOP/lib/Xaw/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xaw normal-object dependency is missing" >&2
            exit 1
        }
    fi

    for library in $BUILD_SET
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

echo "=== validate AMIX shared-data archives ==="
for library in Xt Xmu Xaw
do
    cd "$TOP/lib/$library"
    if ar t "lib$library.so" >/dev/null 2>&1
    then
        echo "lib$library.so: AMIX shared-data archive"
    else
        echo "amix-shared-rebuild: lib$library.so is not an archive" >&2
        exit 1
    fi
done

echo "=== rebuild dynamic clients ==="
for program in $PROGRAMS
do
    cd "$TOP/programs/$program"
    if test "$program" = twm
    then
        rm -f lex.c lex.yy.c lex.o
    fi
    rm -f "$program"
    make "$program"
    test -s "$program"
    ldd "$program"
done

echo "AMIX X11R6 shared libraries and clients are ready."
