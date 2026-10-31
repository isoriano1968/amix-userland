#!/bin/sh

set -e

MODE=${1-libs}

build_dir()
{
    dir=$1
    echo "=== $dir ==="
    touch "$dir/depend"
    (cd "$dir" && make -f Makefile.X11 amix)
}

build_target()
{
    dir=$1
    target=$2
    echo "=== $dir/$target ==="
    touch "$dir/depend"
    (cd "$dir" && make -f Makefile.X11 amix "AMIX_TARGETS=$target")
}

build_libraries()
{
    if test -d lib
    then
        :
    else
        mkdir lib
    fi

    build_dir src
    build_dir src-glu
    build_dir src-glut
}

case "$MODE" in
libs)
    build_libraries
    ;;
xdemos)
    build_dir xdemos
    ;;
bounce)
    build_target demos bounce
    ;;
gears)
    build_target demos gears
    ;;
glutmech|mech)
    build_target demos glutmech
    ;;
demos)
    build_dir demos
    ;;
all)
    build_libraries
    build_dir xdemos
    build_dir demos
    ;;
clean)
    make clean
    ;;
*)
    echo "usage: $0 [libs|xdemos|bounce|gears|glutmech|mech|demos|all|clean]" >&2
    exit 1
    ;;
esac
