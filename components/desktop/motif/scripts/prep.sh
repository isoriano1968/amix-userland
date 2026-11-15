#!/bin/sh
# prep.sh -- unpack and patch Motif 2.3.8, and build the tools Motif runs
# while it builds (makestrs, wml, wmluiltok) for the build host.
#
# The host build only compiles those tools: it needs a host C compiler, flex
# and the host's X11 headers (Motif's configure insists on them), nothing for
# AMIX.
set -e
. "$(dirname "$0")/common.sh"
need "$HOST_CC" patch flex
mkdir -p "$WORK"

echo "== unpack and patch Motif"
rm -rf "$MOTIF_SRC"
tar -C "$WORK" -xzf "$TOP/motif-$MOTIF_VER.tar.gz"
for p in "$TOP"/patches/*.patch; do patch -s -d "$MOTIF_SRC" -p1 < "$p"; done

echo "== host tools (makestrs, wml)"
rm -rf "$MOTIF_HOST"; mkdir -p "$MOTIF_HOST"
(cd "$MOTIF_HOST" && CC="$HOST_CC" "$MOTIF_SRC/configure" --disable-shared --disable-xft --disable-jpeg \
	--disable-png --disable-utf8 > configure.log 2>&1) || { echo "host configure failed: $MOTIF_HOST/configure.log (host X11 development files?)"; exit 1; }
${MAKE:-make} -C "$MOTIF_HOST/tools/wml" wml wmluiltok > "$MOTIF_HOST/make-wml.log" 2>&1
${MAKE:-make} -C "$MOTIF_HOST/config/util" makestrs > "$MOTIF_HOST/make-makestrs.log" 2>&1
ls -l "$MOTIF_HOST/config/util/makestrs" "$MOTIF_HOST/tools/wml/wml"
