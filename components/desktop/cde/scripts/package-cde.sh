#!/bin/sh
# package-cde.sh -- the CDE install tree and tarball for AMIX:
#   $WORK/dtroot/  and  $WORK/cde-amix.tar
# On the AMIX machine, as root:
#   cd / && tar xf cde-amix.tar && sh /usr/dt/AMIX-INSTALL.sh
set -e
. "$(dirname "$0")/common.sh"
O=$WORK/dtroot
rm -rf "${O:?}"
# CDE's own install databases decide what goes where (cde-udb-stage.py)
python3 -I "$S/cde-udb-stage.py" "$CDE" "$O" CDE-MIN CDE-RUN CDE-TT CDE-ICONS \
	CDE-HELP-RUN CDE-HELP-C CDE-MSG-C CDE-FONTS CDE-C
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
# without XKeysymDB Motif's osf* keys (Help, Cancel, menu arrows) are unknown
mkdir -p "$O/usr/x11r6/lib/X11"
cp "$X11_SRC/lib/X11/XKeysymDB" "$X11_SRC/lib/X11/XErrorDB" "$O/usr/x11r6/lib/X11/"
# startcde: a session without dtlogin (xhold keeps the server from resetting)
m68k-elf-gcc -O2 -w -I"$X11_EXPORTS/include" -o "$O/usr/dt/bin/xhold" "$TOP/target/xhold.c" \
	"$X11_EXPORTS/lib/libX11.a" -lsocket -lnsl
m68k-elf-strip "$O/usr/dt/bin/xhold"
cp "$TOP/target/startcde" "$O/usr/dt/bin/startcde" && chmod 555 "$O/usr/dt/bin/startcde"
cp "$TOP/target/xinitrc.amix" "$O/usr/dt/config/xinitrc.amix" && chmod 555 "$O/usr/dt/config/xinitrc.amix"
# AMIX's tar: plain ustar, numeric owners; AMIX-INSTALL.sh sets the real ones
(cd "$O" && tar --format=ustar --numeric-owner --owner=2 --group=2 -cf "$WORK/cde-amix.tar" usr)
[ -s "$O/MISSING" ] && echo "not built (expected, see README): $(wc -l < "$O/MISSING") files, listed in $O/MISSING"
ls -l "$WORK/cde-amix.tar"
