# common.sh -- sourced by the build scripts: config.sh, checks, paths.
# the repository root: the calling script is in it (fetch.sh) or in scripts/
TOP=$(cd "$(dirname "$0")" && pwd)
[ -f "$TOP/config.sh.example" ] || TOP=$(cd "$TOP/.." && pwd)
[ -f "$TOP/config.sh" ] || { echo "no config.sh: cp config.sh.example config.sh and edit it" >&2; exit 1; }
. "$TOP/config.sh"
: "${TOOLCHAIN:?}" "${SYSROOT:?}" "${X11_EXPORTS:?}" "${X11_SRC:?}" "${DL:?}" "${WORK:?}"
# build-shared-cde.sh builds in a tree of its own
[ -z "$SHARED_WORK" ] || { WORK=$SHARED_WORK; }
JOBS=${JOBS:-4}
export PATH="$TOOLCHAIN/bin:$PATH"
S=$TOP/scripts
MOTIF_VER=2.3.8
CDE_VER=2.2.4a
MOTIF_SRC=$WORK/motif-$MOTIF_VER          # unpacked and patched
MOTIF_HOST=$WORK/motif-host               # host build: makestrs, wml
MOTIF_BUILD=$WORK/motif-amix              # cross build
MOTIF_ROOT=$WORK/motif-root               # its install tree (DESTDIR)
LIBXP=$WORK/libxp
CDE=$WORK/cde-$CDE_VER                    # unpacked, patched, built in place
need() { for t in "$@"; do command -v "$t" >/dev/null 2>&1 || { echo "missing on the build host: $t" >&2; exit 1; }; done; }
