#!/bin/sh
# stage-dev.sh STAGE -- what development needs beside the shared libraries:
# headers in each prefix, and .pc files that say all a program
# must link, on the machine itself as on the PC.
set -e
STAGE=$1
TOP=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$TOP/../../.." && pwd)
PRIV=$(cd "$ROOT/../private/shared-build" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
XI=$PRIV/xc/exports/include
XL=$STAGE/usr/x11r6/lib
MR=$TOP/../motif/work/motif-root/usr/motif
CI=$TOP/work-shared/cde-2.2.4a/exports/include
GL=$ROOT/components/x11/mesa-31/work-pic/include/GL

cp "$ROOT/components/developer/gcc-27/include/"std*.h "$STAGE/usr/local/include/"
mkdir -p "$STAGE/usr/x11r6/include" "$STAGE/usr/motif/include" "$STAGE/usr/local/include/GL" \
	"$STAGE/usr/dt/include" "$STAGE/usr/dt/lib/pkgconfig"
cp -RL "$XI/X11" "$STAGE/usr/x11r6/include/"
cp -RL "$MR/include/." "$STAGE/usr/motif/include/"
for h in gl.h gl_mangle.h glu.h glu_mangle.h glut.h glx.h glx_mangle.h; do cp "$GL/$h" "$STAGE/usr/local/include/GL/"; done
for d in Dt Tt csa; do cp -RL "$CI/$d" "$STAGE/usr/dt/include/"; done
chmod -R u+w,go+rX "$STAGE/usr/x11r6/include" "$STAGE/usr/motif/include" "$STAGE/usr/local/include/GL" "$STAGE/usr/dt/include"

# the glue a new CDE program links (tt.pc): shared, like every installed
# library, made by amix-mkshlib from -fPIC archives
DL=$STAGE/usr/dt/lib
GW=$TOP/build/glue; rm -rf "$GW"; mkdir -p "$GW"
for g in amixcxx:$TOP/files/amixcxx.c amixcde:$TOP/work-shared/cde-2.2.4a/lib/amixcde/amixcde.c; do
	n=${g%%:*}
	m68k-elf-gcc -O2 -m68060 -fPIC -c "${g#*:}" -o "$GW/$n.o"
	m68k-elf-ar rcs "$GW/lib$n.a" "$GW/$n.o"
	"$ROOT/tools/amix-mkshlib" "$GW/lib$n.a" lib$n.so.1 "$DL" -L"${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/local/lib" -lamixc99
done
rm -f "$DL/libamixcxx.a" "$DL/libamixcde.a"

# dependencies; Requires are read by both pkg-configs
need() {	# file requires
	f=$1; shift
	grep -q '^Requires:' "$f" || sed -i "/^Version:/a Requires: $*" "$f"
}
P=$XL/pkgconfig
need "$P/x11.pc" amixc99
sed -i 's/ -lX11$/ -lX11 -lsocket -lnsl/' "$P/x11.pc"
sed -i '/-lXtInherit/!s/ -lXt$/ -lXtInherit -lXt/' "$P/xt.pc"
need "$P/xt.pc" sm ice x11; need "$P/sm.pc" ice x11; need "$P/ice.pc" x11
need "$P/xext.pc" x11; need "$P/xi.pc" xext x11; need "$P/xmu.pc" xt xext x11
need "$P/xaw.pc" xmu xt xext x11
P=$STAGE/usr/local/lib/pkgconfig
need "$P/gl.pc" xext x11; need "$P/glu.pc" gl; need "$P/glut.pc" glu gl xmu xi
need "$P/zzgl.pc" x11
sed -i 's/^Requires: xt x11$/Requires: xmu xt xext x11/' "$STAGE/usr/motif/lib/pkgconfig/xm.pc"

# name, library, what it needs
cpc() {
	cat > "$STAGE/usr/dt/lib/pkgconfig/$1.pc" <<P
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
sed -i 's/ -ltt$/ -ltt -lamixcxx -lamixcde/' "$STAGE/usr/dt/lib/pkgconfig/tt.pc"
cpc csa csa "xt x11"
cpc dtsvc DtSvc "tt xm"
cpc dthelp DtHelp "dtsvc xm"
cpc dtwidget DtWidget "dthelp dtsvc xm"
cpc dtterm DtTerm "dthelp dtsvc xm"
cpc dtprint DtPrint "dthelp dtsvc xm"
cpc dtsearch DtSearch "dtsvc"
cpc dtmrm DtMrm "mrm xm"
