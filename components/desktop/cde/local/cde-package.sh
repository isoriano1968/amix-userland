#!/bin/sh
# cde-package.sh -- the CDE install tree and tarball for AMIX, from build-cde.
#   sh cde-package.sh            -> build-cde/dtroot/, build-cde/cde-amix.tar
# On the A3000:  cd / && tar xf cde-amix.tar && sh /usr/dt/AMIX-INSTALL.sh
set -e
H=$(cd "$(dirname "$0")" && pwd)
B=$H/build-cde; O=$B/dtroot
rm -rf "${O:?}"
python3 -I "$H/cde-udb-stage.py" "$B" "$O" CDE-MIN CDE-RUN CDE-TT CDE-ICONS \
	CDE-HELP-RUN CDE-HELP-C CDE-MSG-C CDE-FONTS CDE-C
# ToolTalk type sources: compiled on the target (no host tt_type_comp)
mkdir -p "$O/usr/dt/appconfig/tttypes/src"
cp "$B"/programs/tttypes/*.ptype "$B"/programs/tttypes/dtinfo_start.opnums "$O/usr/dt/appconfig/tttypes/src/"
cp "$H/cde-amix/AMIX-INSTALL.sh" "$O/usr/dt/AMIX-INSTALL.sh"
# fonts.dir lists dtinfo.pcf; the .udb wants a compressed copy nobody builds
cp "$B/programs/fontaliases/linux/C/dtinfo.pcf" "$O/usr/dt/config/xfonts/C/dtinfo.pcf"
# Linux's aliases with the interface "system" font as Lucida Sans (as on
# Solaris) instead of LucidaBright; every target exists in AMIX's 75/100dpi
cp "$H/cde-amix/fonts.alias.amix" "$O/usr/dt/config/xfonts/C/fonts.alias"
# R6.3 Xlib (linked into every dt program) reads these from its ProjectRoot;
# without XKeysymDB Motif's osf* keys (Help, Cancel, menu arrows) are unknown
mkdir -p "$O/usr/x11r6/lib/X11"
cp "$H/xzz9000-z3/src/xc/lib/X11/XKeysymDB" "$H/xzz9000-z3/src/xc/lib/X11/XErrorDB" "$O/usr/x11r6/lib/X11/"
# old tar: plain ustar, numeric owners; AMIX-INSTALL.sh sets the real ones
(cd "$O" && tar --format=ustar --numeric-owner --owner=2 --group=2 -cf "$B/cde-amix.tar" usr)
ls -la "$B/cde-amix.tar"
