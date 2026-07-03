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
REPAIR_SHDATA=no

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
            echo "=== regenerate affected library Makefiles ==="
            for library in Xt Xmu Xaw
            do
                cd "$TOP/lib/$library"
                rm -f Makefile.new
                ../../config/imake/imake -I../../config/cf \
                    -s Makefile.new -f Imakefile \
                    -DTOPDIR=../.. -DCURDIR=lib/$library
                test -s Makefile.new
                mv Makefile.new Makefile
            done

            echo "=== regenerate affected client Makefiles ==="
            for program in $PROGRAMS
            do
                cd "$TOP/programs/$program"
                rm -f Makefile.new
                ../../config/imake/imake -I../../config/cf \
                    -s Makefile.new -f Imakefile \
                    -DTOPDIR=../.. -DCURDIR=programs/$program
                test -s Makefile.new
                mv Makefile.new Makefile
            done
            BUILD_LIBRARIES=yes
            BUILD_SET="Xt Xmu Xaw"
            REPAIR_SHDATA=yes
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
        grep 'UNSHAREDOBJS *= *sharedlib.o' \
            "$TOP/lib/Xt/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xt Makefile does not use the native stub-only layout" >&2
            exit 1
        }
        grep 'AMIXSHLIB' "$TOP/lib/Xt/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xt AMIX wrapper rule is missing" >&2
            exit 1
        }
    fi

    if echo " $BUILD_SET " | grep ' Xmu ' >/dev/null
    then
        grep 'UNSHAREDOBJS *= *sharedlib.o' \
            "$TOP/lib/Xmu/Makefile" >/dev/null || {
            echo "amix-shared-rebuild: Xmu Makefile does not use the native stub-only layout" >&2
            exit 1
        }
    fi

    if grep '^libXaw.sa:' "$TOP/lib/Xaw/Makefile" >/dev/null
    then
        echo "amix-shared-rebuild: Xaw must not generate a shared-data archive on AMIX" >&2
        exit 1
    fi

    grep 'libXt.sa' "$TOP/programs/xclock/Makefile" >/dev/null || {
        echo "amix-shared-rebuild: xclock link does not include libXt.sa" >&2
        exit 1
    }
    grep 'XMULIBONLY.*libXmu.sa' "$TOP/programs/xsetroot/Makefile" >/dev/null || {
        echo "amix-shared-rebuild: xsetroot link does not include libXmu.sa" >&2
        exit 1
    }

    if test "$REPAIR_SHDATA" = yes
    then
        echo "=== restore native ELF .so links ==="
        for library in Xt Xmu Xaw
        do
            cd "$TOP/lib/$library"
            image=
            for candidate in lib$library.so.*
            do
                if test -f "$candidate" && test -s "$candidate"
                then
                    image=$candidate
                    break
                fi
            done
            if test "x$image" = x
            then
                echo "amix-shared-rebuild: missing versioned lib$library ELF image" >&2
                exit 1
            fi
            rm -f lib$library.so
            ln -s "$image" lib$library.so
            rm -f "$TOP/exports/lib/lib$library.so"
            cd "$TOP/exports/lib"
            ln -s ../../lib/$library/lib$library.so .
        done

        echo "=== repair native Xt shared-data archive ==="
        cd "$TOP/lib/Xt"
        rm -f libXt.sa sharedlib.o
        make libXt.sa

        echo "=== repair native Xmu shared-data archive ==="
        cd "$TOP/lib/Xmu"
        rm -f libXmu.sa sharedlib.o
        make libXmu.sa

        rm -f "$TOP/lib/Xaw/libXaw.sa" "$TOP/exports/lib/libXaw.sa"
    else
        for library in $BUILD_SET
        do
            dir="$TOP/lib/$library"
            echo "=== clean PIC rebuild of shared lib$library ==="
            cd "$dir"
            rm -f lib$library.so lib$library.so.* lib$library.sa \
                *.o shared/*.o unshared/*.o
            make
        done
    fi
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
    file "lib$library.so"
    dump -Lv "lib$library.so" >/dev/null
done

echo "=== validate AMIX shared-data archives ==="
for library in Xt Xmu
do
    cd "$TOP/lib/$library"
    members=`ar t "lib$library.sa"`
    if test "x$members" = xsharedlib.o
    then
        echo "lib$library.sa: native AMIX stub archive"
    else
        echo "amix-shared-rebuild: unexpected lib$library.sa members: $members" >&2
        exit 1
    fi
done

if test -f "$TOP/lib/Xaw/libXaw.sa"
then
    echo "amix-shared-rebuild: Xaw unexpectedly has a .sa archive" >&2
    exit 1
fi

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
