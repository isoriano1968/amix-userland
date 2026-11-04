#!/bin/sh
# build-shared.sh STAGE -- libXm and libMrm as shared libraries and mwm
# linked to them, against the shared X of x11/x11r6.3's build-shared.sh in
# STAGE.  Needs build-static.sh's work tree (the sources and host tools).
# Installs to STAGE/usr/motif; libtool does not know this target, so the
# libraries are made from -fPIC archives.
set -e
. "$(dirname "$0")/common.sh"
STAGE=$1
XL=${X_STAGE:-$STAGE}/usr/x11r6/lib
X=$X11_EXPORTS
PIC=$WORK/motif-pic
ROOT=$STAGE/usr/motif
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int -Wno-error=return-mismatch"
CF0="-O2 -fPIC -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fno-strict-aliasing -fcommon"
CF="$CF0 $W"
RPATH="-Wl,-R,/usr/motif/lib:/usr/x11r6/lib -Wl,-rpath-link,$XL -Wl,-rpath-link,$ROOT/lib"

rm -rf "$PIC"; mkdir -p "$PIC" "$ROOT/lib/pkgconfig" "$ROOT/bin"
cp "$S/motif-amix.cache" "$PIC/amix.cache"
(cd "$PIC" && "$MOTIF_SRC/configure" --cache-file="$PIC/amix.cache" \
	--host=m68k-elf --build="$(gcc -dumpmachine)" --prefix=/usr/motif \
	--disable-shared --enable-static --disable-xft --disable-jpeg --disable-png --disable-utf8 \
	--x-includes="$X/include" --x-libraries="$XL" CC=m68k-elf-gcc \
	CPPFLAGS="-I$X/include" LDFLAGS="-L$XL" CFLAGS="$CF0" \
	LIBS="-lXext -lSM -lICE -lX11 -lsocket -lnsl" > configure.log 2>&1) \
	|| { echo "configure failed: $PIC/configure.log"; exit 1; }
cd "$PIC"
cp "$MOTIF_HOST/config/util/makestrs" config/util/
cp "$MOTIF_HOST/tools/wml/wml" "$MOTIF_HOST/tools/wml/wmluiltok" tools/wml/
touch config/util/makestrs tools/wml/wml tools/wml/wmluiltok
make -C lib/Xm XmStrDefs.c XmStrDefs.h XmStrDefs22.h XmStrDefs23.h XmStrDefsI.h > make-strs.log 2>&1
make -j"$JOBS" -C lib CFLAGS="$CF" > make-lib.log 2>&1 || { echo "failed: $PIC/make-lib.log"; exit 1; }
"$TOP/../../../tools/amix-got-check" $(find lib -name '*.o')

m68k-elf-gcc -shared -Wl,-h,libXm.so.4 -Wl,--whole-archive lib/Xm/.libs/libXm.a -Wl,--no-whole-archive \
	-L"$XL" -lXmu -lXt -lSM -lICE -lXext -lX11 $RPATH -o "$ROOT/lib/libXm.so.4"
"$TOP/../../../tools/amix-fix-data-relocs" "$ROOT/lib/libXm.so.4"
"$TOP/../../../tools/amix-fix-needed" "$ROOT/lib/libXm.so.4"
ln -sf libXm.so.4 "$ROOT/lib/libXm.so"
m68k-elf-gcc -shared -Wl,-h,libMrm.so.4 -Wl,--whole-archive lib/Mrm/.libs/libMrm.a -Wl,--no-whole-archive \
	-L"$ROOT/lib" -lXm -L"$XL" -lXt -lX11 $RPATH -o "$ROOT/lib/libMrm.so.4"
"$TOP/../../../tools/amix-fix-needed" "$ROOT/lib/libMrm.so.4"
ln -sf libMrm.so.4 "$ROOT/lib/libMrm.so"
for l in Xm Mrm; do
	cat > "$ROOT/lib/pkgconfig/$(echo $l | tr 'A-Z' 'a-z').pc" <<P
prefix=/usr/motif
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $l
Description: Motif 2.3.8 lib$l
Version: 2.3.8
Requires: xmu xt xext x11
Libs: -L\${libdir} -l$l -lgen
Cflags: -I\${includedir}
P
done

echo "== mwm"
make -j"$JOBS" -C clients/mwm CFLAGS="$CF" \
	LIBS="-L$ROOT/lib -lXm -L$XL -lXmu -lXt -lSM -lICE -lXext -lX11 -lgen -lsocket -lnsl $RPATH" \
	LDFLAGS="-L$ROOT/lib -L$XL" > make-mwm.log 2>&1 || true
ls -l clients/mwm/.libs 2>/dev/null | head -3
m68k-elf-gcc clients/mwm/*.o clients/mwm/WmWsmLib/libWsm.a -L"$ROOT/lib" -lXm -L"$XL" -lXmu -lXt -lSM -lICE -lXext -lX11 -lgen -lsocket -lnsl \
	$RPATH -o "$ROOT/bin/mwm"
m68k-elf-strip "$ROOT/bin/mwm" "$ROOT/lib/libXm.so.4" "$ROOT/lib/libMrm.so.4"
ls -l "$ROOT/lib/libXm.so.4" "$ROOT/lib/libMrm.so.4" "$ROOT/bin/mwm"
