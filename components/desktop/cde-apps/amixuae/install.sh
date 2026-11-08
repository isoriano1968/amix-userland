#!/bin/sh
# install.sh -- (on the A3000, as root, in a copy of cde-apps/amixuae with
# cde-apps/panel beside it) put Amiga (amixuae) in place: the program, its
# CDE files and the control in the AMIX panel.  The emulator itself
# (z3660ARM_Core_uae.bin, c1uae setuid root) comes from emulators/uae4arm.
# .adf/.adz/.dms, .hdf and Kickstart .rom files get their icons and Open in
# the File Manager.  Then Restart Workspace Manager; the Application Manager entry comes with
# the next login (or Application Manager, Reload Applications).
set -e
cp amixuae /opt/amix/bin/amixuae.new
chmod 755 /opt/amix/bin/amixuae.new
mv /opt/amix/bin/amixuae.new /opt/amix/bin/amixuae
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C /etc/dt/appconfig/appmanager/C/Desktop_Apps; do
	[ -d $d ] || mkdir -p $d     # AMIX mkdir -p fails on existing ones
done
cp amixuae.dt /etc/dt/appconfig/types/C/
cp icons/*.pm /etc/dt/appconfig/icons/C/     # AmixUAE and the file types: AmigaADF, AmigaHDF, AmigaROM
: > /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixUAE
chmod 755 /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixUAE
echo "installed: /opt/amix/bin/amixuae, amixuae.dt (action + .adf/.hdf/.rom types), icons, Desktop_Apps/AmixUAE"
echo "File Manager shows the new types after File > Reload Actions (or the next login)"
[ -u /opt/amix/bin/c1uae ] || echo "note: /opt/amix/bin/c1uae (emulators/uae4arm, setuid root) is not installed yet"
# every CDE application of ours is in the AMIX subpanel (panel/amix.fp)
(cd ../panel && sh install.sh)
