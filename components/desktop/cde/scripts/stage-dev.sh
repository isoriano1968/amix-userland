#!/bin/sh
# stage-dev.sh STAGE -- CDE's development files beside its shared libraries:
# the Dt, Tt and csa headers in /usr/dt/include, the glue libraries a new
# CDE program links (libamixcxx, libamixcde), and .pc files that say all such
# a program must link, on the machine itself as on the PC.  X's and Motif's
# headers and .pc files are their own components'.
set -e
. "$(dirname "$0")/common.sh"
STAGE=${1:?usage: stage-dev.sh STAGE}
CI=$CDE/exports/include
DL=$STAGE/usr/dt/lib

mkdir -p "$STAGE/usr/dt/include" "$DL/pkgconfig"
for d in Dt Tt csa; do cp -RL "$CI/$d" "$STAGE/usr/dt/include/"; done
chmod -R u+w,go+rX "$STAGE/usr/dt/include"

# the glue a new CDE program links (tt.pc): shared, like every installed
# library, made by amix-mkshlib from -fPIC archives
GW=$TOP/build/glue; rm -rf "$GW"; mkdir -p "$GW"
for g in amixcxx:$TOP/files/amixcxx.c amixcde:$CDE/lib/amixcde/amixcde.c; do
	n=${g%%:*}
	$CC14 -O2 $CPU -fPIC -c "${g#*:}" -o "$GW/$n.o"
	m68k-elf-ar rcs "$GW/lib$n.a" "$GW/$n.o"
	CC="$CC14" "$TOOLS/amix-mkshlib" "$GW/lib$n.a" lib$n.so.1 "$DL" -L"$SYSROOT/usr/local/lib" -lamixc99
done
rm -f "$DL/libamixcxx.a" "$DL/libamixcde.a"

# name, library, what it needs
cpc() {
	cat > "$DL/pkgconfig/$1.pc" <<P
prefix=/usr/dt
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $2
Description: CDE lib$2
Version: 2.2.4
Requires: $3
Libs: -L\${libdir} -l$2
Cflags: -I\${includedir}
P
}
cpc tt tt "xt x11"
sed -i 's/ -ltt$/ -ltt -lamixcxx -lamixcde/' "$DL/pkgconfig/tt.pc"
cpc csa csa "xt x11"
cpc dtsvc DtSvc "tt xm"
cpc dthelp DtHelp "dtsvc xm"
cpc dtwidget DtWidget "dthelp dtsvc xm"
cpc dtterm DtTerm "dthelp dtsvc xm"
cpc dtprint DtPrint "dthelp dtsvc xm"
cpc dtsearch DtSearch "dtsvc"
cpc dtmrm DtMrm "mrm xm"
