#!/bin/bash
# fix-x-libraries.sh -- repaired copies of AMIX's X11 static libraries.
#
# Every relocatable object in these 1991 archives has its .rela.* section's
# sh_link pointing at itself instead of at .symtab.  GNU ld then silently drops
# those relocations and copies the section into the output's .rela.dyn by name,
# so pointers stay zero and the program jumps to address 0 the first time it
# uses one.  See docs/TOOLCHAIN.md.
#
# WHY THIS WRITES TO ITS OWN DIRECTORY AND NOT TO THE SYSROOT
# ----------------------------------------------------------
# The first version of this script repaired the archives in place, inside
# $AMIX_SYSROOT.  That sysroot is shared with other projects, and editing it in
# place made this build depend on an undocumented modification to someone
# else's tree -- one whose own Makefile does not install usr/X at all, so a
# fresh build of the sysroot would produce no X libraries and therefore no
# repair, silently.
#
# A sibling project had already been bitten by this exact shape: a cross
# compiler carrying an uncommitted local change that every build in the
# repository had been born from, with nothing gating it.  An edit being
# CORRECT is not what makes it safe; what makes it unsafe is that nothing
# downstream can tell it happened.
#
# So the repaired archives are built into a directory this project owns,
# derived from the vendor originals every time, and the sysroot is left as
# found.  A caller links against $OUT rather than the sysroot's lib directory.
#
# Usage: tools/fix-x-libraries.sh [output-directory]
#        (default: $TC_PREFIX/amix-xlib-fixed)
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "fix-x-libraries.sh: no config.sh" >&2; exit 1; }
. ./config.sh
: "${AMIX_SYSROOT:?}" "${TC_PREFIX:?}"
AR="$TC_PREFIX/bin/${TC_TARGET:-m68k-elf}-ar"
XLIB="$AMIX_SYSROOT/usr/X/lib"
[ -d "$XLIB" ] || { echo "fix-x-libraries.sh: no $XLIB" >&2; exit 1; }
OUT="${1:-$TC_PREFIX/amix-xlib-fixed}"
mkdir -p "$OUT"
REPO=$(pwd)

for lib in libX11.a libXext.a libXaw.a libXmu.a libXt.a; do
	src="$XLIB/$lib"
	# Prefer a .orig if one is there: an earlier version of this script
	# repaired in place, and on such a tree the plain name is already fixed.
	[ -f "$src.orig" ] && src="$src.orig"
	[ -f "$src" ] || continue

	work=$(mktemp -d)
	# Extract from the vendor archive every time, so a repeated run cannot
	# compound an earlier one and the result depends only on the input.
	( cd "$work" && "$AR" x "$src" ) 2>/dev/null
	n=$(cd "$work" && python3 "$REPO/tools/fix-elf-relalink.py" *.o 2>/dev/null | tail -1)
	( cd "$work" && "$AR" rcs "$lib" *.o && mv "$lib" "$OUT/$lib" )
	printf "  %-12s %s\n" "$lib" "$n"
	rm -rf "$work"
done

echo
echo "fix-x-libraries.sh: repaired archives are in $OUT"
echo "  Link with -L$OUT BEFORE the sysroot's own X library directory."
