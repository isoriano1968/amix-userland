#!/bin/bash
# install-fixed-headers.sh -- repair AMIX system headers without editing them.
#
# GCC searches its own include-fixed directory before the sysroot, which is how
# it copes with system headers that are wrong or, here, simply older than the
# standard.  Each header in compat/include/fixed/ reaches the target's real one
# with #include_next and adds what is missing, so nothing in the vendor tree is
# modified and C and C++ both get the repair.
#
# Two kinds of repair live here.  compat/include/fixed/ holds hand-written
# wrappers that add what a header is missing (the C99 classification macros in
# <math.h>).  make-kandr-fixes.py generates the other kind: AMIX defines stat(),
# lstat(), fstat() and uname() as K&R functions inside the headers, which C++
# cannot parse at all, so those headers are rewritten into ANSI form from the
# local sysroot.  The generated ones contain AMIX's code and must stay out of
# version control -- they land in the toolchain, not in this repository.
#
# Must run before libstdc++ is built: without the <math.h> macros its own C++17
# sources do not compile.
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "install-fixed-headers.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
[ -x "$CC" ] || { echo "install-fixed-headers.sh: no $CC" >&2; exit 1; }

GCCVER=$("$CC" -dumpversion)
FIXED="$TC_PREFIX/lib/gcc/$TC_TARGET/$GCCVER/include-fixed"
mkdir -p "$FIXED"
while IFS= read -r rel; do
	mkdir -p "$FIXED/$(dirname "$rel")"
	cp "compat/include/fixed/$rel" "$FIXED/$rel"
	echo "install-fixed-headers.sh: $rel (hand-written)"
done < <(cd compat/include/fixed && find . -name '*.h' -printf '%P\n' | sort)

python3 toolchain/make-kandr-fixes.py "$AMIX_SYSROOT" "$FIXED"

# Linkage guards after the repairs above, so it skips the headers they wrote
# (each carries its own guard) -- but BEFORE the additions below.  An addition
# that brings its own extern "C" block, as stdio.h's does, makes the header
# look already guarded, and the rest of it is then left with C++ linkage.
# Measured on a build from zero: libstdc++ stopped on 'int system(const
# char*)' declared with C++ linkage in stdio.h and C linkage in stdlib.h.
toolchain/make-cxx-wrappers.sh "$AMIX_SYSROOT" "$FIXED"

# Additions to headers that fixincludes has already copied: those copies shadow
# the sysroot, so a #include_next wrapper would never reach them and replacing
# them would discard whatever fixincludes repaired.  Appending is the one option
# left.  Idempotent via the marker each addition carries.
if [ -d compat/include/fixed-additions ]; then
	while IFS= read -r rel; do
		add="compat/include/fixed-additions/$rel"
		marker=$(grep -m1 "^#ifndef _AMIX_HAVE" "$add" | awk '{print $2}')
		[ -n "$marker" ] || { echo "no _AMIX_HAVE marker in $add" >&2; exit 1; }
		if [ -f "$FIXED/$rel" ] && ! grep -q "$marker" "$FIXED/$rel"; then
			cat "$add" >> "$FIXED/$rel"
			echo "install-fixed-headers.sh: $rel (appended $marker)"
		fi
	done < <(cd compat/include/fixed-additions && find . -name '*.h' -printf '%P\n' | sort)
fi
# AMIX has no <stdint.h> at all.  GCC's own one for this target, and the
# C++ wrapper make-cxx-wrappers.sh writes for it, #include_next the C
# library's header, which does not exist; so the compiler carries the whole
# file here (compat/include/stdint.h, ours).  The sysroot stays the AMIX
# system as it is (developer/amix/sysroot on OpenIndiana does the same).
cp compat/include/stdint.h "$FIXED/stdint.h"
echo "install-fixed-headers.sh: stdint.h (compat/include, whole)"
# not a fixed header but libamixc99's own: amix_cpu() (compat/amix_cpu.c)
cp compat/include/amix_cpu.h "$FIXED/amix_cpu.h"
echo "install-fixed-headers.sh: amix_cpu.h (compat/include, libamixc99's)"
echo "install-fixed-headers.sh: -> $FIXED"
