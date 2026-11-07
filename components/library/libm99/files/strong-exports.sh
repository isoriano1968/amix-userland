#!/bin/sh
# strong-exports.sh OBJ EXPORTS: make OBJ's symbols global only for the names
# in EXPORTS (all others local), and the weak ones among them (musl's
# weak_alias: lgamma_r ...) strong: AMIX's 1991 ld.so.1 is older than any use
# of weak definitions in a shared library, so nothing relies on how it treats
# them.  objcopy's --globalize-symbol keeps a weak symbol weak, so each is
# taken out and added again as a global at the same place.
set -e
o=$1; ex=$2
OBJCOPY=${OBJCOPY:-m68k-elf-objcopy}
READELF=${READELF:-m68k-elf-readelf}
$OBJCOPY --keep-global-symbols="$ex" "$o"
text=`$READELF -SW "$o" | sed -n 's/^ *\[ *\([0-9]*\)\] \.text .*/\1/p'`
args=
for s in `$READELF -sW "$o" | awk -v t="$text" '$5 == "WEAK" && $7 != "UND" { if ($7 != t || $4 != "FUNC") { print "BAD:" $8; next } print $8 "=" $2 }'`; do
	case $s in BAD:*) echo "strong-exports.sh: ${s#BAD:} is not a function in .text" >&2; exit 1;; esac
	n=${s%%=*}; v=${s#*=}
	args="$args --strip-symbol=$n --add-symbol=$n=.text:0x$v,global,function"
done
[ -z "$args" ] || $OBJCOPY $args "$o"
