#!/bin/sh

TOP=${1-/usr/x11r6/xc}
MODE=${2-useful}

USEFUL_PROGRAMS="xinit xsetroot xclock xterm twm xdpyinfo xhost xset xwd xwininfo xlsfonts xlsclients xauth xkill xrefresh xmodmap xprop xrdb xlogo xmag xstdcmap oclock mkfontdir bdftopcf rgb"

COMMON_PROGRAMS="appres bdftopcf bitmap editres fsinfo fslsfonts fstobdf iceauth mkfontdir oclock proxymngr rgb x11perf xauth xclipboard xclock xcmsdb xdpyinfo xfd xhost xkill xlogo xlsatoms xlsclients xlsfonts xmag xmodmap xprop xrdb xrefresh xset xsetroot xstdcmap xterm xwd xwininfo xwud twm xinit"

OPTIONAL_PROGRAMS="lbxproxy xconsole xdm xfs xfindproxy xfwp xieperf xkbcomp xkbevd xkbprint xkbutils xmh xrx xsm"

cd "$TOP" || exit 1

if test -d programs
then
    :
else
    echo "amix-build-programs: $TOP is not an X11R6 source tree" >&2
    exit 1
fi

if test "x$LD_LIBRARY_PATH" = x
then
    LD_LIBRARY_PATH=$TOP/exports/lib
else
    LD_LIBRARY_PATH=$TOP/exports/lib:$LD_LIBRARY_PATH
fi
export LD_LIBRARY_PATH

case "$MODE" in
useful)
    PROGRAMS=$USEFUL_PROGRAMS
    ;;
common)
    PROGRAMS=$COMMON_PROGRAMS
    ;;
all)
    PROGRAMS="$COMMON_PROGRAMS $OPTIONAL_PROGRAMS"
    ;;
*)
    echo "usage: $0 [source-tree [useful|common|all]]" >&2
    exit 1
    ;;
esac

failures=

for program in $PROGRAMS
do
    dir=$TOP/programs/$program

    if test -d "$dir"
    then
        :
    else
        echo "=== $program: skipped, no source directory ==="
        continue
    fi

    if test -f "$dir/Makefile"
    then
        :
    else
        echo "=== $program: skipped, no Makefile ==="
        failures="$failures $program"
        continue
    fi

    echo "=== $program ==="
    cd "$dir" || exit 1

    if test "$program" = twm
    then
        rm -f lex.c lex.yy.c lex.o
    fi

    if make
    then
        :
    else
        failures="$failures $program"
        if test "x$AMIX_PROGRAMS_KEEP_GOING" = xno
        then
            break
        fi
    fi
done

if test "x$failures" = x
then
    echo "AMIX X11R6 program build completed."
    exit 0
fi

echo "AMIX X11R6 program build had failures:$failures" >&2
exit 1
