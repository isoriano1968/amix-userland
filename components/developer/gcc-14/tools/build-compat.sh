#!/bin/bash
# build-compat.sh -- build libamixc99.a, the parts of a C library that AMIX
# SVR4.0 does not have, and install it into the toolchain.
#
# It is installed rather than left in a build directory, and named in the
# compiler's own lib spec, because these are libc functions: code that calls
# snprintf should link, without every project having to know that this target
# needs an extra library.  That is also why the name says what it is -- the C99
# and POSIX pieces AMIX predates -- rather than naming a project.
#
# Not a Makefile: config.sh is shell, and a Makefile that `include`s it gets the
# quotes as part of the value.
#
# The name is deliberately not libamixcompat.a: another AMIX cross toolchain
# installs one of those into the shared sysroot, holding syscall wrappers and
# floating-point control, and overwriting it would break every other project
# using that sysroot.
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "build-compat.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"

if [ -x "$TC_PREFIX/bin/$TC_TARGET-gcc" ]; then
	CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
	AR="$TC_PREFIX/bin/$TC_TARGET-ar"
	INC=""
else
	echo "build-compat.sh: using the fallback compiler ($MODERN_GCC)"
	CC="$MODERN_GCC"
	AR="${MODERN_PREFIX}ar"
	INC="-nostdinc -isystem $AMIX_SYSROOT/usr/include"
fi

OUT=build/compat
mkdir -p "$OUT"
# amix_init.c is a STARTFILE, not a library member: it is named in the specs
# and linked into every program, because nothing would ever reference it.
$CC $AMIX_CPU -O2 $INC -U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
	-c -o "$OUT/amix_init.o" compat/amix_init.c
# mkdir rather than "install only if the directory happens to exist": skipping
# the install in silence leaves a toolchain whose specs name a startfile that
# is not there, and the failure surfaces much later as a link error in an
# unrelated project.
mkdir -p "$TC_PREFIX/$TC_TARGET/lib"
cp "$OUT/amix_init.o" "$TC_PREFIX/$TC_TARGET/lib/amix_init.o"
echo "build-compat.sh: installed $TC_PREFIX/$TC_TARGET/lib/amix_init.o"

for src in compat/*.c; do
	[ "$src" = "compat/amix_init.c" ] && continue
	obj="$OUT/$(basename "${src%.c}").o"
	$CC $AMIX_CPU -O2 -fno-strict-aliasing $INC \
		-U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
		-c -o "$obj" "$src"
done
rm -f "$OUT/libamixc99.a"
$AR rcs "$OUT/libamixc99.a" "$OUT"/*.o

if [ -d "$TC_PREFIX/$TC_TARGET/lib" ]; then
	cp "$OUT/libamixc99.a" "$TC_PREFIX/$TC_TARGET/lib/libamixc99.a"
	echo "build-compat.sh: installed $TC_PREFIX/$TC_TARGET/lib/libamixc99.a"
else
	echo "build-compat.sh: $OUT/libamixc99.a (toolchain not built yet, not installed)"
fi
