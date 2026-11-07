#!/bin/sh
# m060-group.sh OUT "EXPORTS" "ALIASES" "EXTERNALS" OBJ...
#
# One relocatable object OUT of the musl objects OBJ..., as libm060.a's
# member for one AMIX libm member: only EXPORTS stay global, everything else
# (musl's exp, log, sin, __rem_pio2, tables, __math_oflow ...) becomes local,
# so it never meets a program's names, AMIX's libm or libm99's.  Each ALIAS
# new=old is a second global name at old's address (AMIX's _j0, _lgamma ...,
# which its jn.o and others call).  The undefined symbols left must be among
# EXTERNALS (lib060's sqrt ...), or it fails.
# Tools from the environment: LD, OBJCOPY, NM (m68k-elf-* or the host's).
set -e
out=$1 ex=$2 al=$3 extern=$4
shift 4
LD=${LD:-ld} OBJCOPY=${OBJCOPY:-objcopy} NM=${NM:-nm}
$LD -r -o "$out" "$@"
keep=$out.keep
for s in $ex; do echo $s; done > "$keep"
$OBJCOPY --keep-global-symbols="$keep" "$out"
rm -f "$keep"
args=
for a in $al; do
	n=${a%%=*} o=${a#*=}
	v=`$NM "$out" | awk -v s="$o" '$3 == s && $2 == "T" { print $1 }'`
	[ -n "$v" ] || { echo "m060-group.sh: $o not a global function in $out" >&2; exit 1; }
	args="$args --add-symbol=$n=.text:0x$v,global,function"
done
[ -z "$args" ] || $OBJCOPY $args "$out"
for s in $ex; do
	$NM "$out" | awk -v s="$s" '$3 == s && $2 ~ /^[TDBR]$/ { f = 1 } END { exit !f }' ||
		{ echo "m060-group.sh: $s not a strong global in $out" >&2; exit 1; }
done
bad=`$NM -u "$out" | awk '{ print $2 }' | while read u; do
	case " $extern " in *" $u "*) ;; *) echo $u;; esac; done`
[ -z "$bad" ] || { echo "m060-group.sh: $out needs" $bad >&2; exit 1; }
