#!/bin/sh
# package-motif.sh -- mwm and its configuration for AMIX: $WORK/motif-amix.tar
# (/usr/motif/bin/mwm, /usr/motif/lib/X11/system.mwmrc, and the Mwm
# app-defaults in /usr/x11r6/lib/X11/app-defaults).  Not needed for CDE, whose
# programs link Motif statically.  On AMIX:  cd / && tar xf motif-amix.tar
set -e
. "$(dirname "$0")/common.sh"
O=$WORK/motif-pkg
rm -rf "${O:?}"; mkdir -p "$O/usr/motif/bin"
m68k-elf-strip -o "$O/usr/motif/bin/mwm" "$MOTIF_ROOT/usr/motif/bin/mwm"
mkdir -p "$O/usr/motif/lib"
cp -R "$MOTIF_ROOT/usr/motif/lib/X11" "$O/usr/motif/lib/"     # system.mwmrc
# mwm's colours (see target/motif/Mwm); R6.3 Xt reads app-defaults from here
mkdir -p "$O/usr/x11r6/lib/X11/app-defaults"
cp "$TOP/target/motif/Mwm" "$O/usr/x11r6/lib/X11/app-defaults/Mwm"
(cd "$O" && tar --format=ustar --numeric-owner --owner=2 --group=2 -cf "$WORK/motif-amix.tar" usr)
tar tf "$WORK/motif-amix.tar"
