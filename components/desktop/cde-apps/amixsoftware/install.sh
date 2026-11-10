#!/bin/sh
# install.sh -- (on the A3000, as root, in a copy of cde-apps/amixsoftware
# with cde-apps/panel beside it) put the AMIX Software Center in place: the
# window, the setuid apkg runner swrun, its CDE files and its control in the
# AMIX panel.  Then Restart Workspace Manager.
set -e
L=/opt/amix/lib/amixsoftware
[ -d $L ] || mkdir -p $L	# AMIX mkdir -p fails on existing ones
cp amixsoftware /opt/amix/bin/amixsoftware.new
chmod 755 /opt/amix/bin/amixsoftware.new
mv /opt/amix/bin/amixsoftware.new /opt/amix/bin/amixsoftware
cp swrun $L/swrun.new
/usr/bin/chown root $L/swrun.new
/usr/bin/chgrp sys $L/swrun.new
chmod 4755 $L/swrun.new
mv $L/swrun.new $L/swrun
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C /etc/dt/appconfig/appmanager/C/Desktop_Apps; do
	[ -d $d ] || mkdir -p $d
done
cp amixsoftware.dt /etc/dt/appconfig/types/C/
cp icons/AmixSoftware.l.pm icons/AmixSoftware.m.pm icons/AmixSoftware.t.pm /etc/dt/appconfig/icons/C/
: > /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixSoftware
chmod 755 /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixSoftware
echo "installed: /opt/amix/bin/amixsoftware, $L/swrun (setuid root),"
echo "  amixsoftware.dt, Software Center icons, Desktop_Apps/AmixSoftware"
(cd ../panel && sh install.sh)
