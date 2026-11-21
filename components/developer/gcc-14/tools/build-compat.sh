#!/bin/bash
# build-compat.sh -- build libamixc99.so.1, the parts of a C library that AMIX
# SVR4.0 does not have, and amix_init.o, and install them into the toolchain.
#
# They are installed rather than left in a build directory, and named in the
# compiler's own lib spec, because these are libc functions: code that calls
# snprintf should link, without every project having to know that this target
# needs an extra library.  That is also why the name says what it is -- the C99
# and POSIX pieces AMIX predates -- rather than naming a project.
#
# A SHARED library, like every library we install: libamixc99.so.1, which
# library/amixc99 delivers to /usr/lib on AMIX, beside libc.so.1, in the one
# directory ld.so.1 searches by default (its only built-in path), so no
# program needs a runpath for it, not even one started from an rc script.
# One copy in the process: the program and every library that calls snprintf
# use the same one, and __cxa_atexit keeps one list for all of them.
# The toolchain links against the copy installed here
# ($TC_PREFIX/$TC_TARGET/lib, beside libstdc++.a), so it links for any
# sysroot; the file is the same one the package delivers.
#
# It is self-contained: it calls only what libc.so.1 exports (checked after
# the link), never a member of AMIX's link-time archive (select,
# _sys_siglist ...), so it loads whether a program NEEDs it itself or only
# through another library (pkg -> libz.so.1 -> libamixc99.so.1).
#
# amix_init.o is not part of it: a startfile of every program (see below).
#
# Not a Makefile: config.sh is shell, and a Makefile that `include`s it gets the
# quotes as part of the value.
#
# The name is deliberately not libamixcompat: another AMIX cross toolchain
# installs a libamixcompat.a into the shared sysroot, holding syscall wrappers
# and floating-point control, and overwriting it would break every other
# project using that sysroot.
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "build-compat.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"

if [ -x "$TC_PREFIX/bin/$TC_TARGET-gcc" ]; then
	CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
	INC=""
else
	echo "build-compat.sh: using the fallback compiler ($MODERN_GCC)"
	CC="$MODERN_GCC"
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

# The library needs the cross compiler's driver (its specs make the -shared
# link) and libgcc (__udivdi3 for %llu): build-cross.sh runs this after
# libgcc is installed and before libstdc++, whose configure links.
if [ -z "$INC" ] && [ -f "$("$CC" -print-libgcc-file-name)" ]; then
	TOOLS="$(pwd)/../../../tools"
	rm -f "$OUT/libamixc99.a" "$OUT"/libamixc99.so*
	objs=
	for src in compat/*.c; do
		[ "$src" = "compat/amix_init.c" ] && continue
		obj="$OUT/$(basename "${src%.c}").o"
		$CC $AMIX_CPU -O2 -fPIC -fno-strict-aliasing \
			-U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
			-c -o "$obj" "$src"
		objs="$objs $obj"
	done
	READELF="$TC_PREFIX/bin/$TC_TARGET-readelf" "$TOOLS/amix-got-check" $objs
	# -nodefaultlibs: the lib spec would add -lamixc99, this library itself.
	# libc.so.1 is named as a file, as the spec does for every -shared link;
	# libgcc's __udivdi3/__umoddi3 are hidden there, so they stay inside.
	$CC -shared -nodefaultlibs -Wl,-h,libamixc99.so.1 $objs \
		"$AMIX_SYSROOT/usr/lib/libc.so.1" -lgcc -o "$OUT/libamixc99.so.1" 2>&1 |
		grep -v '/usr/lib/libc.so.1, needed by' >&2 || true
	[ -f "$OUT/libamixc99.so.1" ]
	# Self-contained: every symbol it leaves undefined must be an export of
	# libc.so.1.  What only AMIX's link-time archive has (select, _sys_siglist,
	# gettimeofday ...) would have to come from the program, and a program
	# that loads this library only through another one never has it.
	"$TC_PREFIX/bin/$TC_TARGET-readelf" -sW "$AMIX_SYSROOT/usr/lib/libc.so.1" |
		awk '$7 != "UND" && $5 != "LOCAL" && NF == 8 { print $8 }' | sort -u > "$OUT/libc.exports"
	"$TC_PREFIX/bin/$TC_TARGET-readelf" -W --dyn-syms "$OUT/libamixc99.so.1" |
		awk '$7 == "UND" && NF == 8 { print $8 }' | sort -u > "$OUT/libamixc99.undef"
	missing=$(comm -23 "$OUT/libamixc99.undef" "$OUT/libc.exports")
	if [ -n "$missing" ]; then
		echo "build-compat.sh: libamixc99.so.1 needs what libc.so.1 does not export:" $missing >&2
		exit 1
	fi
	"$TC_PREFIX/bin/$TC_TARGET-strip" "$OUT/libamixc99.so.1"
	"$TOOLS/amix-fix-data-relocs" "$OUT/libamixc99.so.1"
	"$TOOLS/amix-fix-needed" "$OUT/libamixc99.so.1" > /dev/null
	ln -sf libamixc99.so.1 "$OUT/libamixc99.so"
	mkdir -p "$TC_PREFIX/$TC_TARGET/lib"
	# no archive any more, anywhere: a stale one would be linked instead
	rm -f "$TC_PREFIX/$TC_TARGET/lib/libamixc99.a"
	cp "$OUT/libamixc99.so.1" "$TC_PREFIX/$TC_TARGET/lib/libamixc99.so.1.new"
	mv "$TC_PREFIX/$TC_TARGET/lib/libamixc99.so.1.new" "$TC_PREFIX/$TC_TARGET/lib/libamixc99.so.1"
	ln -sf libamixc99.so.1 "$TC_PREFIX/$TC_TARGET/lib/libamixc99.so"
	echo "build-compat.sh: installed $TC_PREFIX/$TC_TARGET/lib/libamixc99.so.1"
else
	echo "build-compat.sh: no libgcc (or the fallback compiler) yet: libamixc99.so.1 not built"
fi
