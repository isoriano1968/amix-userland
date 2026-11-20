# common.sh -- sourced by the build scripts: paths, and the tools on PATH
#
#   AMIX_SYSROOT   a clean sysroot with x11/x11r6.3, x11/libxp and desktop/motif
#                  in it (developer/amix/sysroot and those packages on OpenIndiana)
#   AMIX_TOOLS     gcc-14's bin directory
#   AMIX_CPU       the CPU option every AMIX object is compiled with
#   HOST_CC, HOST_CXX  the build host's compilers, for imake and CDE's tools
#   CDE_ARCHIVE, MOTIF_ARCHIVE, MOTIF_DIR, XC_ARCHIVE  the pinned inputs
#                  (the Makefile checks them and passes them on)
TOP=$(cd "$(dirname "$0")/.." && pwd)
SYSROOT=${AMIX_SYSROOT:?AMIX_SYSROOT is not set}
AMIX_TOOLS=${AMIX_TOOLS:-$HOME/opt/m68k-amix/bin}
CPU=${AMIX_CPU:--mcpu=68020 -mtune=68060 -fexcess-precision=fast}
HOST_CC=${HOST_CC:-gcc}
HOST_CXX=${HOST_CXX:-g++}
export PATH="$AMIX_TOOLS:$PATH"
# gcc 14 sees only the sysroot it is given, never its built-in default
CC14="m68k-elf-gcc --sysroot=$SYSROOT"
X11=$SYSROOT/usr/x11r6                    # x11/x11r6.3 and x11/libxp
MOTIF=$SYSROOT/usr/motif                  # desktop/motif
S=$TOP/scripts
TOOLS=$(cd "$TOP/../../../tools" && pwd)
WORK=$TOP/build/work
OUT=$TOP/build/shared                     # what the build makes: usr/dt/lib
XTI=$TOP/build/xtinherit                  # libXtInherit.a for the programs
CDE_VER=2.2.4a
MOTIF_VER=2.3.8
CDE=$WORK/cde-$CDE_VER                    # unpacked, patched, built in place
MOTIF_SRC=$WORK/motif-$MOTIF_VER          # unpacked and patched: lib/Xm
export CDE
need() { for t in "$@"; do command -v "$t" >/dev/null 2>&1 || { echo "missing on the build host: $t" >&2; exit 1; }; done; }
"$TOOLS/amix-sysroot-check" "$SYSROOT" || exit 1
for f in "$X11/include/X11/Xlib.h" "$X11/include/X11/extensions/Print.h" "$X11/lib/libXt.so.7" \
	"$X11/lib/libXp.so.7" "$MOTIF/include/Xm/Xm.h" "$MOTIF/lib/libXm.so.4"; do
	[ -f "$f" ] || { echo "$SYSROOT has no ${f#$SYSROOT}: install x11/x11r6.3, x11/libxp and desktop/motif first" >&2; exit 1; }
done
