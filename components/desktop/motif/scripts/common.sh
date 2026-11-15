# common.sh -- sourced by the build scripts: paths, and the tools on PATH
#
#   AMIX_SYSROOT   a clean sysroot with x11/x11r6.3 in it (developer/amix/sysroot
#                  + developer/amix/x11r6.3 on OpenIndiana)
#   AMIX_TOOLS     gcc-14's bin directory
#   AMIX_CPU       the CPU option every object is compiled with
#   HOST_CC        the build host's C compiler, for Motif's own tools
# C collation: globs such as clients/mwm/*.o (mwm's link order) sort the same
# on every build host
LC_ALL=C; export LC_ALL
TOP=$(cd "$(dirname "$0")/.." && pwd)
SYSROOT=${AMIX_SYSROOT:?AMIX_SYSROOT is not set}
CPU=${AMIX_CPU:--m68060}
HOST_CC=${HOST_CC:-gcc}
X11=$SYSROOT/usr/x11r6                    # X11R6.3: include/, lib/
WORK=$TOP/build/work
JOBS=${JOBS:-8}
export PATH="${AMIX_TOOLS:-$HOME/opt/m68k-amix/bin}:$PATH"
# gcc 14 sees only the sysroot it is given, never its built-in default
CC14="m68k-elf-gcc --sysroot=$SYSROOT"
S=$TOP/files
MOTIF_VER=2.3.8
MOTIF_SRC=$WORK/motif-$MOTIF_VER          # unpacked and patched
MOTIF_HOST=$WORK/motif-host               # host build: makestrs, wml
MOTIF_PIC=$WORK/motif-pic                 # cross build, -fPIC
need() { for t in "$@"; do command -v "$t" >/dev/null 2>&1 || { echo "missing on the build host: $t" >&2; exit 1; }; done; }
"$TOP/../../../tools/amix-sysroot-check" "$SYSROOT" || exit 1
[ -f "$X11/include/X11/Xlib.h" ] && [ -f "$X11/lib/libXt.so.7" ] ||
	{ echo "$SYSROOT has no x11/x11r6.3 (usr/x11r6): install it first" >&2; exit 1; }
