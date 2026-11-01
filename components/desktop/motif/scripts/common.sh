# common.sh -- sourced by the build scripts: paths, and the tools on PATH
TOP=$(cd "$(dirname "$0")/.." && pwd)
SYSROOT=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}
TOOLCHAIN=${AMIX_TOOLCHAIN:-$HOME/opt/m68k-amix}
X11_EXPORTS=${X11_EXPORTS:-$SYSROOT/usr/x11r6/xc/exports}
WORK=$TOP/work
JOBS=${JOBS:-8}
export PATH="$TOOLCHAIN/bin:$PATH"
S=$TOP/files
MOTIF_VER=2.3.8
MOTIF_SRC=$WORK/motif-$MOTIF_VER          # unpacked and patched
MOTIF_HOST=$WORK/motif-host               # host build: makestrs, wml
MOTIF_BUILD=$WORK/motif-amix              # cross build
MOTIF_ROOT=$WORK/motif-root               # its install tree (DESTDIR)
need() { for t in "$@"; do command -v "$t" >/dev/null 2>&1 || { echo "missing on the build host: $t" >&2; exit 1; }; done; }
