#!/bin/sh
# shared-cde-libs.sh CDE STAGE -- CDE's libraries as shared ones, from their
# -fPIC archives, next to the archives so that -l finds them first (make still
# wants the .a).  None has static constructors, so no .init is needed; what a
# library needs, it names; libstdc++ and libc's stub stay with the program.
set -e
CDE=$1; STAGE=$2
TOOLS=$(cd "$(dirname "$0")/../../../.." && pwd)/tools
L=$STAGE/usr/dt/lib
mkdir -p "$L"
X="-L$STAGE/usr/motif/lib -L$STAGE/usr/x11r6/lib -Wl,-rpath-link,$STAGE/usr/motif/lib -Wl,-rpath-link,$STAGE/usr/x11r6/lib -Wl,-rpath-link,$L"
for a in tt/lib/libtt.a DtSvc/libDtSvc.a DtHelp/libDtHelp.a DtWidget/libDtWidget.a DtTerm/libDtTerm.a \
	DtPrint/libDtPrint.a DtMrm/libDtMrm.a DtSearch/libDtSearch.a csa/libcsa.a; do
	case $a in
	tt/*)		deps="-lX11" ;;
	DtSvc/*)	deps="-ltt -lXm -lXt -lX11" ;;
	DtHelp/*)	deps="-lDtSvc -ltt -lXm -lXt -lX11" ;;
	DtSearch/*)	deps="-lDtSvc" ;;
	csa/*)		deps="-ltt" ;;
	*)		deps="-lDtHelp -lDtSvc -ltt -lXm -lXt -lX11" ;;
	esac
	d=$CDE/lib/$(dirname "$a")
	n=$(basename "$a" .a)
	[ -f "$d/$n.a" ] || { echo "no $a"; continue; }
	rm -rf "$d/.x"; mkdir "$d/.x"
	(cd "$d/.x" && m68k-elf-ar x "../$n.a")
	if ! "$TOOLS/amix-got-check" "$d"/.x/*.o > "$d/gotcheck.log"; then
		echo "$n: bad GOT relocations: $d/gotcheck.log"; rm -rf "$d/.x"; continue
	fi
	rm -rf "$d/.x"
	m68k-elf-gcc -shared -Wl,-h,$n.so.2 -Wl,--whole-archive "$d/$n.a" -Wl,--no-whole-archive \
		-L"$L" $X $deps -Wl,-R,/usr/dt/lib:/usr/motif/lib:/usr/x11r6/lib -o "$L/$n.so.2"
	"$TOOLS/amix-fix-data-relocs" "$L/$n.so.2"
	ln -sf "$L/$n.so.2" "$d/$n.so.2"
	ln -sf "$n.so.2" "$d/$n.so"
	ln -sf "$n.so.2" "$L/$n.so"
	ln -sf "$L/$n.so.2" "$CDE/exports/lib/$n.so.2"
	ln -sf "$n.so.2" "$CDE/exports/lib/$n.so"
	echo "$n: $(wc -c < "$L/$n.so.2") bytes"
done
