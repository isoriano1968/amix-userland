#!/bin/sh
# build-shared.sh STAGE -- libXm and libMrm as shared libraries (libXm.so.4,
# libMrm.so.4), their headers and .pc files, and mwm linked to them, against
# the shared X11R6.3 (libX*.so.7) in the sysroot.  Needs prep.sh's tree (the
# sources and host tools).  Installs to STAGE/usr/motif; libtool does not
# know this target, so the libraries are made from -fPIC archives.
#
# configure cannot run test programs for AMIX; files/motif-amix.cache holds
# the answers (sprintf returns int, stack grows down, no X threads, ...).
# GCC 14 turns several 1990s C idioms into errors that Motif and the X11R6.3
# headers rely on; on AMIX they are safe as warnings (see gcc14-cross-amix).
# --disable-printing: no X Print in Motif (as in use on AMIX), whether or not
# the sysroot already holds x11/libxp; configure would otherwise follow it.
set -e
. "$(dirname "$0")/common.sh"
STAGE=${1:?usage: build-shared.sh STAGE}
XI=$X11/include
XL=$X11/lib
ROOT=$STAGE/usr/motif
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type"
# -ffile-prefix-map: assert()'s __FILE__ (FontS.c) names the source, not the
# build machine's directory, so every build host makes the same library
CF0="-O2 $CPU -fPIC -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fno-strict-aliasing -fcommon -ffile-prefix-map=$MOTIF_SRC=motif-$MOTIF_VER"
CF="$CF0 $W"
RPATH="-Wl,-R,/usr/motif/lib:/usr/x11r6/lib -Wl,-rpath-link,$XL -Wl,-rpath-link,$ROOT/lib"

rm -rf "$MOTIF_PIC" "$ROOT"; mkdir -p "$MOTIF_PIC" "$ROOT/lib/pkgconfig" "$ROOT/lib/X11" "$ROOT/bin"
cp "$S/motif-amix.cache" "$MOTIF_PIC/amix.cache"
(cd "$MOTIF_PIC" && "$MOTIF_SRC/configure" --cache-file="$MOTIF_PIC/amix.cache" \
	--host=m68k-elf --build="$($HOST_CC -dumpmachine)" --prefix=/usr/motif \
	--disable-shared --enable-static --disable-xft --disable-jpeg --disable-png --disable-utf8 --disable-printing \
	--x-includes="$XI" --x-libraries="$XL" CC="$CC14" \
	CPPFLAGS="-I$XI" LDFLAGS="-L$XL" CFLAGS="$CF0" \
	LIBS="-lXext -lSM -lICE -lX11 -lsocket -lnsl" > configure.log 2>&1) \
	|| { echo "configure failed: $MOTIF_PIC/configure.log"; exit 1; }
cd "$MOTIF_PIC"
cp "$MOTIF_HOST/config/util/makestrs" config/util/
cp "$MOTIF_HOST/tools/wml/wml" "$MOTIF_HOST/tools/wml/wmluiltok" tools/wml/
touch config/util/makestrs tools/wml/wml tools/wml/wmluiltok
# -j1 here and for the installs: their rules link and copy the same files
# from more than one target (a MAKEFLAGS=-j from outside races)
${MAKE:-make} -j1 -C lib/Xm XmStrDefs.c XmStrDefs.h XmStrDefs22.h XmStrDefs23.h XmStrDefsI.h > make-strs.log 2>&1
${MAKE:-make} -j"$JOBS" -C lib CFLAGS="$CF" > make-lib.log 2>&1 || { echo "failed: $MOTIF_PIC/make-lib.log"; exit 1; }
"$TOP/../../../tools/amix-got-check" $(find lib -name '*.o')

echo "== libXm.so.4, libMrm.so.4"
$CC14 -shared -Wl,-h,libXm.so.4 -Wl,--whole-archive lib/Xm/.libs/libXm.a -Wl,--no-whole-archive \
	-L"$XL" -lXmu -lXt -lSM -lICE -lXext -lX11 $RPATH -o "$ROOT/lib/libXm.so.4"
"$TOP/../../../tools/amix-fix-data-relocs" "$ROOT/lib/libXm.so.4"
"$TOP/../../../tools/amix-fix-needed" "$ROOT/lib/libXm.so.4"
ln -sf libXm.so.4 "$ROOT/lib/libXm.so"
$CC14 -shared -Wl,-h,libMrm.so.4 -Wl,--whole-archive lib/Mrm/.libs/libMrm.a -Wl,--no-whole-archive \
	-L"$ROOT/lib" -lXm -L"$XL" -lXt -lX11 $RPATH -o "$ROOT/lib/libMrm.so.4"
"$TOP/../../../tools/amix-fix-needed" "$ROOT/lib/libMrm.so.4"
ln -sf libMrm.so.4 "$ROOT/lib/libMrm.so"
for l in Xm Mrm; do
	case $l in Xm) req="xmu xt xext x11" ;; Mrm) req="xt x11" ;; esac
	cat > "$ROOT/lib/pkgconfig/$(echo $l | tr 'A-Z' 'a-z').pc" <<P
prefix=/usr/motif
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $l
Description: Motif 2.3.8 lib$l
Version: 2.3.8
Requires: $req
Libs: -L\${libdir} -l$l -lgen
Cflags: -I\${includedir}
P
done

echo "== headers"
# Xm's and Mrm's headers are installed with their libraries (lib/*/Makefile):
# a scratch install, of which only include/ is kept
rm -rf "$WORK/inst"
${MAKE:-make} -j1 -C lib install DESTDIR="$WORK/inst" > make-install-lib.log 2>&1
${MAKE:-make} -j1 -C include install DESTDIR="$WORK/inst" > make-install-inc.log 2>&1
cp -R "$WORK/inst/usr/motif/include" "$ROOT/"
rm -rf "$WORK/inst"

echo "== mwm"
${MAKE:-make} -j"$JOBS" -C clients/mwm CFLAGS="$CF" \
	LIBS="-L$ROOT/lib -lXm -L$XL -lXmu -lXt -lSM -lICE -lXext -lX11 -lgen -lsocket -lnsl $RPATH" \
	LDFLAGS="-L$ROOT/lib -L$XL" > make-mwm.log 2>&1 || true
$CC14 clients/mwm/*.o clients/mwm/WmWsmLib/libWsm.a -L"$ROOT/lib" -lXm -L"$XL" -lXmu -lXt -lSM -lICE -lXext -lX11 -lgen -lsocket -lnsl \
	$RPATH -o "$ROOT/bin/mwm"
cp "$MOTIF_SRC/clients/mwm/system.mwmrc" "$ROOT/lib/X11/system.mwmrc"
m68k-elf-strip "$ROOT/bin/mwm" "$ROOT/lib/libXm.so.4" "$ROOT/lib/libMrm.so.4"
ls -l "$ROOT/lib/libXm.so.4" "$ROOT/lib/libMrm.so.4" "$ROOT/bin/mwm"
