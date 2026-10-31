#!/bin/bash
# install-specs.sh -- turn the generic m68k-elf compiler into an AMIX compiler.
#
# WHY
# ---
# GCC has no m68k SVR4 target any more, so build-cross.sh configures the generic
# m68k-elf one.  Its specs are bare: startfile is just crtbegin.o, lib is just
# -lc, link is empty.  So `m68k-elf-gcc -o prog prog.c` fails with
# "cannot find -lc" -- AMIX keeps its link-time libc stub in usr/ccs/lib, which
# is not on the default search path -- and even if it linked, the result would
# have no process entry point, no interpreter and the wrong text address.
#
# That is not just an inconvenience.  libstdc++'s configure runs link tests; a
# compiler that cannot link makes it set GCC_NO_EXECUTABLES and then abort with
# "Link tests are not allowed after GCC_NO_EXECUTABLES", which is exactly where
# the first build of this toolchain stopped.
#
# A specs file is the supported way to retarget a GCC without patching it: the
# driver reads $libdir/specs in preference to its built-in strings.  Everything
# below is guarded on %{!nostdlib:...}, so tools/amix-cc.sh -- which passes
# -nostdlib and names every object itself -- keeps full control and is
# unaffected by what is written here.
#
# %R expands to the configured sysroot, so the file stays portable between
# machines.
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "install-specs.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
[ -x "$CC" ] || { echo "install-specs.sh: no $CC -- run toolchain/build-cross.sh first" >&2; exit 1; }

# Derive the driver's lib directory from the prefix rather than from
# -print-libgcc-file-name: before libgcc is built that prints a bare "libgcc.a",
# whose dirname is "." -- and the specs then land in the current directory,
# where nothing reads them.
GCCVER=$("$CC" -dumpversion)
LIBDIR="$TC_PREFIX/lib/gcc/$TC_TARGET/$GCCVER"
[ -d "$LIBDIR" ] || { echo "install-specs.sh: no $LIBDIR" >&2; exit 1; }
SPECS="$LIBDIR/specs"

"$CC" -dumpspecs > "$SPECS.new"

python3 - "$SPECS.new" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()

def replace(section, value):
    global text
    key = "*%s:\n" % section
    i = text.index(key) + len(key)
    j = text.index("\n\n", i)
    text = text[:i] + value + text[j:]

# crt1.o is the AMIX process entry and calls main; crti.o opens .init/.fini.
# crtbegin.o must come after them: it is what registers .eh_frame with the
# unwinder, which is how C++ exceptions work here without loader support.
# amix_init.o goes after crtbegin.o and before anything else: it contributes an
# .init fragment that walks .init_array, which AMIX's 1991 crt1.o has never
# heard of.  Without it NO C++ static constructor runs -- see
# compat/amix_init.c for how a linker-script section rename causes that.
replace("startfile",
        "%{!shared:%{!nostdlib:%{!nostartfiles:%R/usr/ccs/lib/crt1.o %R/usr/ccs/lib/crti.o}}}"
        " crtbegin.o%s %{!nostdlib:%{!nostartfiles:amix_init.o%s}}")
replace("endfile",
        "crtend.o%s %{!shared:%{!nostdlib:%{!nostartfiles:%R/usr/ccs/lib/crtn.o}}}")
# usr/ccs/lib holds libc.so, the link-time stub for the shared libc, and is not
# on any default search path.
# libamixc99 holds what SVR4.0's libc is missing -- snprintf, vsnprintf,
# usleep, __cxa_atexit.  Named before -lc because it calls into libc and not
# the other way round.
replace("lib",
        "%{!nostdlib:%{!nodefaultlibs:-L%R/usr/ccs/lib -L%R/usr/lib -lamixc99 -lc}}")
# The platform identifies itself.  A generic m68k-elf compiler defines nothing
# that says "AMIX", so portable code has no way to select a path for this
# target -- and code that IS written for it, like a crash handler that reads
# the fault PC out of a ucontext, silently compiles to nothing instead.  The
# usual three spellings, as every Unix port does it.
replace("cpp", "-D__amix__ -D__amix -Damix")

# AMIX's system headers are C89 and declare plenty of functions with no return
# type -- X11R5's Xlib.h alone does it for most of the library.  GCC 14 made
# implicit int an ERROR, so a stock compiler cannot include <X11/Xlib.h> on this
# target at all.  Demoted to a warning, and ONLY this one: implicit function
# declarations stay an error, because those come from call sites in our own code
# rather than from the vendor's headers, and letting them through would hide the
# exact class of mistake this target punishes hardest.
# -malign-int IS THE ABI, not a tuning flag.
#
# AMIX's cc rounds a struct up to 4 bytes; m68k GCC rounds to 2.  libc's own
# stdio array settles it: __iob is 320 bytes with _NFILE 20, so its stride is
# 16, and its FILE is int + pointer + pointer + uchar + uchar, which GCC makes
# 14 bytes.  So `stdout`, which is the macro `&__iob[1]`, is __iob+14 in our
# code and __iob+16 in libc's -- two bytes off for stdout and four for stderr.
#
# What that looks like is not a crash.  printf works, because GCC turns it into
# puts and libc indexes its own array correctly.  fprintf(stdout, ...) becomes
# fwrite on a FILE two bytes short of the real one, reads flags that say "not
# writable", and returns having written nothing.  fprintf(stderr, ...) is four
# bytes off, so _base and _ptr come out of the middle of the previous FILE, and
# writing through those wild pointers stops the process and takes its shell
# with it.  A whole program can therefore print its help and then fail silently
# with a clean exit(1) -- which is exactly how OpenTTD behaved here for hours.
#
# It goes in the specs rather than in CFLAGS because it cannot be optional: one
# translation unit compiled without it disagrees with every other about where
# the fields of a shared struct are, and nothing says so at link time.  It also
# means libgcc and libstdc++ must be built by a compiler that already has this
# spec installed -- see toolchain/build-cross.sh.
replace("cc1", "%(cc1_cpu) -Wno-error=implicit-int")
# self_spec, not cc1: it rewrites the command line for EVERY language, so C,
# C++ and libgcc's own compiles all get it from one place.  Guarded on
# -mno-align-int so the flag can still be turned off deliberately -- useful for
# proving the fault again, useless by accident.
#
# -fno-fold-mem-offsets: GCC 14's new fold-mem-offsets pass miscompiles m68k
# indexed addressing.  It folds `p + (i*4 - 4)` into (%a2,%d2.l) and drops the
# -4, so `l[count-1] = v` after `l = realloc(l, count*sizeof *l)` writes one
# element past the block (stock Debian m68k-linux-gnu-gcc 14.2 does the same).
# Found as heap corruption in CDE (DtSvc MMDb.c build_file_list), crashing
# dtfile/dtdbcache/dtwm inside AMIX's malloc.  Every translation unit needs
# it, libgcc and libstdc++ included, hence self_spec as well.
replace("self_spec", "%{!mno-align-int:-malign-int} %{!ffold-mem-offsets:-fno-fold-mem-offsets}")

# Read off the machine's own /usr/bin/ls: libc.so.1 IS the runtime linker, text
# starts at 0x80000000, and the hash table is the classic SysV one.  Nothing
# AMIX ships carries a RELRO segment or a build-id note.
replace("link",
        "%{!nostdlib:--dynamic-linker /usr/lib/libc.so.1 -Ttext-segment=0x80000000"
        " --hash-style=sysv -z norelro --build-id=none}")
open(path, "w").write(text)
PY

mv "$SPECS.new" "$SPECS"
echo "install-specs.sh: $SPECS"
# The verification needs crtbegin.o/crtend.o, which libgcc provides.  During a
# fresh toolchain build this script runs BEFORE libgcc exists -- the specs have
# to be in place for libstdc++'s configure link tests -- so a missing crtbegin
# is expected then, not a failure.
if [ -f "$LIBDIR/crtbegin.o" ]; then
	echo "== a default link should now work and produce an AMIX executable"
	TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
	printf '#include <stdio.h>\nint main(void){printf("default specs work\\n");return 0;}\n' > "$TMP/t.c"
	"$CC" $AMIX_CPU -O2 -o "$TMP/t" "$TMP/t.c"
	file "$TMP/t"
else
	echo "== libgcc is not built yet, so the link test is skipped (expected during a fresh build)"
fi
