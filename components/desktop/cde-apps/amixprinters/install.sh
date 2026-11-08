#!/bin/sh
# install.sh -- (on the A3000, as root, in a copy of cde-apps/amixprinters
# with cde-apps/panel beside it) put Printers in place: the window, the
# search tool, the sender and the setuid lp helper, the test page, its CDE
# files and its control in the AMIX panel.  Then Restart Workspace Manager.
set -e
L=/opt/amix/lib/amixprinters
[ -d $L ] || mkdir -p $L	# AMIX mkdir -p fails on existing ones
inst() {	# inst FILE DEST MODE: as .new, then mv
	cp $1 $2.new
	chmod $3 $2.new
	mv $2.new $2
}
inst amixprinters /opt/amix/bin/amixprinters 755
inst amixprfind $L/amixprfind 755
inst prsend $L/prsend 755
inst testpage.ps $L/testpage.ps 644
cp prset $L/prset.new
/usr/bin/chown root $L/prset.new
/usr/bin/chgrp sys $L/prset.new
chmod 4755 $L/prset.new
mv $L/prset.new $L/prset
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C /etc/dt/appconfig/appmanager/C/Desktop_Apps; do
	[ -d $d ] || mkdir -p $d
done
cp amixprinters.dt /etc/dt/appconfig/types/C/
cp icons/AmixPrinters.l.pm icons/AmixPrinters.m.pm icons/AmixPrinters.t.pm /etc/dt/appconfig/icons/C/
: > /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixPrinters
chmod 755 /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixPrinters
echo "installed: /opt/amix/bin/amixprinters, $L/{amixprfind,prsend,prset (setuid root),testpage.ps},"
echo "  amixprinters.dt, Printers icons, Desktop_Apps/AmixPrinters"
(cd ../panel && sh install.sh)
