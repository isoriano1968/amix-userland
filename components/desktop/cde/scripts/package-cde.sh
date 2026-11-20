#!/bin/sh
# package-cde.sh STAGE -- the CDE install tree for AMIX in STAGE: /usr/dt as
# CDE's own install databases lay it out, and the AMIX additions (ToolTalk
# type sources, AMIX-INSTALL.sh, fonts, XKeysymDB/XErrorDB, xhold, startcde).
# The shared libraries and development files are the Makefile's stage.
# On the AMIX machine, as root:
#   cd / && tar xf cde-amix.tar && sh /usr/dt/AMIX-INSTALL.sh
set -e
. "$(dirname "$0")/common.sh"
O=${1:?usage: package-cde.sh STAGE}
rm -rf "$O"
# CDE's own install databases decide what goes where (cde-udb-stage.py)
python3 -I "$S/cde-udb-stage.py" "$CDE" "$O" CDE-MIN CDE-RUN CDE-TT CDE-ICONS \
	CDE-HELP-RUN CDE-HELP-C CDE-MSG-C CDE-FONTS CDE-C
mv "$O/MISSING" "$TOP/build/MISSING"
# ToolTalk type sources: compiled on the target (tt_type_comp is an AMIX binary)
mkdir -p "$O/usr/dt/appconfig/tttypes/src"
cp "$CDE"/programs/tttypes/*.ptype "$CDE"/programs/tttypes/dtinfo_start.opnums "$O/usr/dt/appconfig/tttypes/src/"
cp "$TOP/target/AMIX-INSTALL.sh" "$O/usr/dt/AMIX-INSTALL.sh"
# fonts.dir lists dtinfo.pcf; the .udb wants a compressed copy nobody builds
cp -f "$CDE/programs/fontaliases/linux/C/dtinfo.pcf" "$O/usr/dt/config/xfonts/C/dtinfo.pcf"
# the interface "system" font as Lucida Sans (as on Solaris); every alias
# target exists in AMIX's 75/100dpi fonts
cp -f "$TOP/target/fonts.alias.amix" "$O/usr/dt/config/xfonts/C/fonts.alias"
# R6.3 Xlib (linked into every dt program) reads these from its ProjectRoot;
# without XKeysymDB Motif's osf* keys (Help, Cancel, menu arrows) are unknown.
# From the pinned X11R6.3 archive (the AMIX port patch does not touch them).
mkdir -p "$O/usr/x11r6/lib/X11"
gzip -dc "${XC_ARCHIVE:?}" | tar -C "$TOP/build" -xf - xc/lib/X11/XKeysymDB xc/lib/X11/XErrorDB
cp "$TOP/build/xc/lib/X11/XKeysymDB" "$TOP/build/xc/lib/X11/XErrorDB" "$O/usr/x11r6/lib/X11/"
rm -rf "$TOP/build/xc"
# startcde: a session without dtlogin (xhold keeps the server from resetting)
$CC14 -O2 $CPU -w -I"$X11/include" -o "$O/usr/dt/bin/xhold" "$TOP/target/xhold.c" \
	-L"$X11/lib" -lX11 -lsocket -lnsl -lamixc99 -Wl,-R,/usr/x11r6/lib
"$TOOLS/amix-fix-needed" "$O/usr/dt/bin/xhold"
m68k-elf-strip "$O/usr/dt/bin/xhold"
cp "$TOP/target/startcde" "$O/usr/dt/bin/startcde" && chmod 555 "$O/usr/dt/bin/startcde"
cp "$TOP/target/xinitrc.amix" "$O/usr/dt/config/xinitrc.amix" && chmod 555 "$O/usr/dt/config/xinitrc.amix"
# the session's root window cursor (an arrow, not X's cross)
cp "$TOP/target/0080.amix-root" "$O/usr/dt/config/Xsession.d/0080.amix-root" && \
	chmod 555 "$O/usr/dt/config/Xsession.d/0080.amix-root"
