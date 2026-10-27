#!/bin/sh
# build-motif.sh -- Motif 2.3.8 for AMIX: static libXm/libMrm and mwm.
#
# Two builds of the same source: a host build for the tools Motif runs while
# it builds (makestrs, wml, wmluiltok), and the cross build for AMIX, which
# uses those host tools.  The result is installed into $WORK/motif-root
# (/usr/motif inside it), where the CDE build takes it from.
#
# configure cannot run test programs for AMIX; motif-amix.cache holds the
# answers (sprintf returns int, stack grows down, no X threads, ...).
# GCC 14 turns several 1990s C idioms into errors that Motif and the X11R6.3
# headers rely on; on AMIX they are safe as warnings (see gcc14-cross-amix).
set -e
. "$(dirname "$0")/common.sh"
need gcc make patch flex
X=$X11_EXPORTS
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type"
CF0="-O2 -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fno-strict-aliasing -fcommon"
CF="$CF0 $W"
mkdir -p "$WORK"

echo "== unpack and patch Motif"
rm -rf "$MOTIF_SRC"
tar -C "$WORK" -xzf "$DL/motif-$MOTIF_VER.tar.gz"
for p in "$TOP"/patches/motif/*.patch; do patch -s -d "$MOTIF_SRC" -p1 < "$p"; done

echo "== host tools (makestrs, wml)"
rm -rf "$MOTIF_HOST"; mkdir -p "$MOTIF_HOST"
(cd "$MOTIF_HOST" && "$MOTIF_SRC/configure" --disable-shared --disable-xft --disable-jpeg \
	--disable-png --disable-utf8 > configure.log 2>&1) || { echo "host configure failed: $MOTIF_HOST/configure.log (host X11 development files?)"; exit 1; }
make -C "$MOTIF_HOST/tools/wml" wml wmluiltok > "$MOTIF_HOST/make-wml.log" 2>&1
make -C "$MOTIF_HOST/config/util" makestrs > "$MOTIF_HOST/make-makestrs.log" 2>&1

echo "== configure for AMIX"
rm -rf "$MOTIF_BUILD"; mkdir -p "$MOTIF_BUILD"
cp "$S/motif-amix.cache" "$MOTIF_BUILD/amix.cache"
(cd "$MOTIF_BUILD" && "$MOTIF_SRC/configure" --cache-file="$MOTIF_BUILD/amix.cache" \
	--host=m68k-elf --build="$(gcc -dumpmachine)" --prefix=/usr/motif \
	--disable-shared --enable-static --disable-xft --disable-jpeg --disable-png --disable-utf8 \
	--x-includes="$X/include" --x-libraries="$X/lib" CC=m68k-elf-gcc \
	CPPFLAGS="-I$X/include" LDFLAGS="-L$X/lib" CFLAGS="$CF0" \
	LIBS="-lXext -lSM -lICE -lX11 -lsocket -lnsl" > configure.log 2>&1) \
	|| { echo "configure failed: $MOTIF_BUILD/configure.log"; exit 1; }

echo "== libXm, libMrm"
cd "$MOTIF_BUILD"
cp "$MOTIF_HOST/config/util/makestrs" config/util/
cp "$MOTIF_HOST/tools/wml/wml" "$MOTIF_HOST/tools/wml/wmluiltok" tools/wml/
touch config/util/makestrs tools/wml/wml tools/wml/wmluiltok
make -C lib/Xm XmStrDefs.c XmStrDefs.h XmStrDefs22.h XmStrDefs23.h XmStrDefsI.h > make-strs.log 2>&1
make -j"$JOBS" -C lib CFLAGS="$CF" > make-lib.log 2>&1 || { echo "failed: $MOTIF_BUILD/make-lib.log"; exit 1; }
rm -rf "$MOTIF_ROOT"
make -C lib install DESTDIR="$MOTIF_ROOT" > make-install-lib.log 2>&1
make -C include install DESTDIR="$MOTIF_ROOT" > make-install-inc.log 2>&1

echo "== mwm"
# Xt and Xmu through R6.3's .sa stubs (their shared objects need them);
# libgen for regcmp/regex
make -j"$JOBS" -C clients/mwm CFLAGS="$CF" \
	LIBS="$X/lib/libXt.sa $X/lib/libXmu.sa -lXext -lSM -lICE -lX11 -lgen -lsocket -lnsl" \
	> make-mwm.log 2>&1 || { echo "failed: $MOTIF_BUILD/make-mwm.log"; exit 1; }
make -C clients/mwm install DESTDIR="$MOTIF_ROOT" > make-install-mwm.log 2>&1
ls -l "$MOTIF_ROOT/usr/motif/lib/libXm.a" "$MOTIF_ROOT/usr/motif/bin/mwm"
