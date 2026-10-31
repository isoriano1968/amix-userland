#!/bin/bash
# amix-cc.sh -- compile and link an AMIX SVR4 executable, C or C++.
#
# Prefers the cross toolchain from toolchain/build-cross.sh; falls back to the
# packaged m68k-linux-gnu compiler, which can only do C (see docs/TOOLCHAIN.md
# for why its libgcc cannot link C++ for this target).
#
# The AMIX-specific knowledge is all here, and every value in it was read off
# the machine's own binaries rather than assumed:
#   interpreter        /usr/lib/libc.so.1     -- on AMIX, libc *is* the rtld
#   text segment       0x80000000
#   hash table         SysV                   -- a .gnu.hash would be unreadable
#   no RELRO segment   nothing AMIX ships has one
#
# Usage: amix-cc.sh -o <output> <sources and flags...>
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "amix-cc.sh: no config.sh -- copy config.sh.example" >&2; exit 1; }
. ./config.sh
: "${AMIX_SYSROOT:?set AMIX_SYSROOT in config.sh}"
[ -d "$AMIX_SYSROOT/usr/include" ] || { echo "amix-cc.sh: no headers in $AMIX_SYSROOT/usr/include" >&2; exit 1; }
CCS="$AMIX_SYSROOT/usr/ccs/lib"
for o in crt1.o crti.o crtn.o; do
	[ -f "$CCS/$o" ] || { echo "amix-cc.sh: missing $CCS/$o" >&2; exit 1; }
done

OUT=""; ARGS=(); WANT_CXX=0
while [ $# -gt 0 ]; do
	case "$1" in
		-o) OUT="$2"; shift 2;;
		*.cpp|*.cc|*.cxx|*.C) WANT_CXX=1; ARGS+=("$1"); shift;;
		*)  ARGS+=("$1"); shift;;
	esac
done
[ -n "$OUT" ] || { echo "amix-cc.sh: -o <output> is required" >&2; exit 1; }

TC_TARGET="${TC_TARGET:-m68k-elf}"
OWN_CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
OWN_CXX="$TC_PREFIX/bin/$TC_TARGET-g++"

if [ -x "$OWN_CXX" ] && [ "$WANT_CXX" = 1 ]; then
	CC="$OWN_CXX"; OWN=1
elif [ -x "$OWN_CC" ]; then
	CC="$OWN_CC"; OWN=1
elif [ "$WANT_CXX" = 1 ]; then
	echo "amix-cc.sh: C++ needs the toolchain from toolchain/build-cross.sh;" >&2
	echo "            the packaged m68k-linux-gnu compiler cannot link it for AMIX." >&2
	exit 1
else
	CC="$MODERN_GCC"; OWN=0
fi

# Include paths.
#  Our own toolchain is configured --with-sysroot=$AMIX_SYSROOT, so its search
#  order is already right: GCC's own fixed headers, then the C++ headers, then
#  the target's usr/include.  Passing -nostdinc there would throw away the C++
#  headers along with the rest, which is exactly the mistake to avoid.
#  The fallback compiler's sysroot is a glibc one, so there we do have to cut
#  the standard paths off and name the AMIX headers explicitly.
if [ "$OWN" = 1 ]; then
	INCFLAGS=""
else
	INCFLAGS="-nostdinc -isystem $AMIX_SYSROOT/usr/include"
fi

CFLAGS_AMIX="$AMIX_CPU $INCFLAGS \
  -U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 \
  -fno-pie -fno-PIC -fno-strict-aliasing"

LDFLAGS_AMIX="-Wl,--dynamic-linker=/usr/lib/libc.so.1 \
  -Wl,-Ttext-segment=0x80000000 -Wl,--hash-style=sysv -Wl,-z,norelro \
  -Wl,--build-id=none -nostdlib -no-pie"

# Startup and shutdown objects, in the order the SVR4 ABI wants them:
#   crt1.o   process entry, calls main
#   crti.o   opens the .init and .fini sections
#   crtbegin.o  GCC's: registers .eh_frame with the unwinder and runs static
#               constructors.  THIS is what makes C++ exceptions work without
#               loader support, so it is not optional for C++ -- and harmless
#               for C.
#   ...objects...
#   crtend.o    closes what crtbegin opened
#   crtn.o      closes .init and .fini
STARTFILES=("$CCS/crt1.o" "$CCS/crti.o")
ENDFILES=("$CCS/crtn.o")
LIBS=(-L"$AMIX_SYSROOT/usr/lib" -L"$CCS")
if [ "$OWN" = 1 ]; then
	GCCDIR=$("$CC" $AMIX_CPU -print-file-name=crtbegin.o)
	GCCDIR=$(dirname "$GCCDIR")
	[ -f "$GCCDIR/crtbegin.o" ] && STARTFILES+=("$GCCDIR/crtbegin.o")
	# amix_init.o must follow crtbegin.o: it adds the .init fragment that walks
	# .init_array, without which no C++ static constructor runs at all.  This
	# wrapper passes -nostdlib and names every startfile itself, so the specs'
	# copy of this rule does not reach it.
	AMIXINIT=$("$CC" $AMIX_CPU -print-file-name=amix_init.o)
	[ -f "$AMIXINIT" ] && STARTFILES+=("$AMIXINIT")
	[ -f "$GCCDIR/crtend.o" ] && ENDFILES=("$GCCDIR/crtend.o" "${ENDFILES[@]}")
	if [ "$WANT_CXX" = 1 ]; then
		# libstdc++ is the containers and strings; libsupc++ underneath it is
		# the runtime proper -- operator new/delete, RTTI, __cxa_throw.  Both
		# are named because a freestanding build installs only the second, and
		# this has to work against either.  They call back into libgcc's
		# unwinder, and libgcc calls malloc and abort in libc, so all of them
		# have to be able to see each other: hence the group rather than a
		# single pass.
		STDCXX=""
		[ -n "$("$CC" $AMIX_CPU -print-file-name=libstdc++.a | grep '/')" ] && STDCXX="-lstdc++"
		# With --disable-shared there is no separate libgcc_eh.a: the
		# unwinder is inside libgcc.a.  Ask rather than assume, so this
		# works against a toolchain built either way.
		EH=""
		[ -n "$("$CC" $AMIX_CPU -print-file-name=libgcc_eh.a | grep '/')" ] && EH="-lgcc_eh"
		# libamixc99 comes from the lib spec, but it has to be inside the
		# group too: __cxa_atexit lives there and libstdc++ calls it.
		LIBS+=(-Wl,--start-group $STDCXX -lsupc++ -lgcc $EH -lamixc99 -lc -Wl,--end-group)
	else
		LIBS+=(-Wl,--start-group -lgcc -lc -Wl,--end-group)
	fi
else
	LIBS+=(-lc "$("$CC" $AMIX_CPU -print-libgcc-file-name)")
fi

[ -n "$AMIX_CC_VERBOSE" ] && set -x
"$CC" $CFLAGS_AMIX $LDFLAGS_AMIX -o "$OUT" \
	"${STARTFILES[@]}" "${ARGS[@]}" "${LIBS[@]}" "${ENDFILES[@]}"
echo "amix-cc.sh: $OUT  [$(basename "$CC")]"
