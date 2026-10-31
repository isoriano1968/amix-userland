#!/bin/bash
# rebuild-target-libs.sh -- rebuild libgcc and libstdc++ after a change that
# affects code generation or the target headers.
#
# WHY THIS IS A SCRIPT AND NOT FOUR COMMANDS
# ------------------------------------------
# Every step here was got wrong by hand first, and none of the three mistakes
# announced itself as a mistake:
#
#   1. `make -j4 all-target-libgcc install-target-libgcc` installs IN PARALLEL
#      with building, so install ran first and failed with "cannot read
#      libgcc.a".  Independent top-level goals are not ordered by make.
#   2. The build tree's xgcc reads `build-gcc/gcc/include-fixed`, not the
#      installed one.  Without it, libstdc++ compiles against the sysroot's raw
#      K&R headers and dies on `sys/stat.h` -- and the error names the sysroot
#      path, which looks like a broken sysroot rather than a missing copy.
#   3. Fixing (2) and re-running only the failed step leaves the objects from
#      before the fix in place.  They link, and the failure appears later as
#      `undefined reference to _Z4freePv` -- a C++-mangled `free`, from a
#      translation unit compiled before the `extern "C"` wrappers were visible.
#
# So the order is: specs, then headers, then a CLEAN of both libraries, then
# build and install strictly sequentially.  A partial rebuild of these two
# libraries is worse than no rebuild, because it produces an archive that is
# half one ABI and half the other and still links.
#
# Usage: toolchain/rebuild-target-libs.sh
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "rebuild-target-libs.sh: no config.sh" >&2; exit 1; }
. ./config.sh
: "${TC_PREFIX:?}"
# Same defaults as build-cross.sh, and read from the same place: TC_WORK in
# config.sh if it is set.  Two scripts disagreeing about where the build tree
# is would fail by rebuilding nothing and reporting success.
WORK="${TC_WORK:-/tmp/m68k-amix-build}"
TARGET="${TC_TARGET:-m68k-elf}"
GCCVER=14.2.0
BUILD="$WORK/build-gcc"

[ -x "$BUILD/gcc/xgcc" ] || {
	echo "rebuild-target-libs.sh: no build tree at $BUILD -- this only does an"
	echo "  incremental rebuild of the target libraries.  For a compiler change"
	echo "  or a missing tree, run toolchain/build-cross.sh instead."
	exit 1
}

echo "== 1/5 installing specs (both drivers need them)"
./toolchain/install-specs.sh >/dev/null
cp "$TC_PREFIX/lib/gcc/$TARGET/$GCCVER/specs" "$BUILD/gcc/specs"

echo "== 2/5 repaired headers, installed and copied into the build tree"
# After a compiler rebuild, `make install-gcc` installs the build tree's own
# fixincludes output over the installed include-fixed, dropping every repaired
# header; libstdc++ then dies in chrono.cc on an undeclared gettimeofday.
./toolchain/install-fixed-headers.sh >/dev/null
# Not a convenience: the build tree's xgcc finds its fixed headers next to
# itself, so the installed copy is invisible to it.
mkdir -p "$BUILD/gcc/include-fixed"
cp -R "$TC_PREFIX/lib/gcc/$TARGET/$GCCVER/include-fixed/." "$BUILD/gcc/include-fixed/"

echo "== 3/5 cleaning both target libraries"
make -C "$BUILD" clean-target-libgcc clean-target-libstdc++-v3

echo "== 4/5 libgcc"
make -C "$BUILD" -j"$(nproc)" MAKEINFO=true all-target-libgcc
make -C "$BUILD" MAKEINFO=true install-target-libgcc

echo "== 5/5 libstdc++"
make -C "$BUILD" -j"$(nproc)" MAKEINFO=true all-target-libstdc++-v3
make -C "$BUILD" MAKEINFO=true install-target-libstdc++-v3

echo
echo "== checking for mangled C library symbols"
# The signature of headers compiled without their extern "C" wrappers.  It
# links fine here and fails in whatever links the library next, which is why it
# is checked at the source.
BAD=$("$TC_PREFIX/bin/$TARGET-nm" "$TC_PREFIX/$TARGET/lib/libstdc++.a" 2>/dev/null \
      | grep -c "_Z4freePv\|_Z6mallocm\|_Z5abortv\|_Z8strerrori" || true)
if [ "$BAD" != "0" ]; then
	echo "rebuild-target-libs.sh: FAILED -- libstdc++.a has $BAD mangled C symbols."
	echo "  The C++ wrappers in include-fixed were not visible to the compile."
	echo "  Run toolchain/install-fixed-headers.sh, then this script again."
	exit 1
fi
echo "rebuild-target-libs.sh: done, no mangled C symbols."
