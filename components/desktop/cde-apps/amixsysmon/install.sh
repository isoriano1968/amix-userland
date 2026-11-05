#!/bin/sh
# install.sh -- (on the A3000, as root, in this directory) put amixsysmon and
# its CDE files in place; then Reload Actions.  Needs library/amixstat
# (libamixstat.so.1), installed with amix-install.  amixsysmon reads
# /dev/kmem, so it is setgid sys.
set -e
cp amixsysmon /opt/amix/bin/amixsysmon
/usr/bin/chgrp sys /opt/amix/bin/amixsysmon
chmod 2755 /opt/amix/bin/amixsysmon
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C /etc/dt/appconfig/appmanager/C/Desktop_Apps; do
	[ -d $d ] || mkdir -p $d        # AMIX mkdir -p fails on existing ones
done
cp amixsysmon.dt /etc/dt/appconfig/types/C/
cp icons/SysMon.l.pm icons/SysMon.m.pm icons/SysMon.t.pm /etc/dt/appconfig/icons/C/
# the Application Manager entry: a file named after the action, executable
: > /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixSysMon
chmod 755 /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixSysMon
echo "installed: /opt/amix/bin/amixsysmon (setgid sys), amixsysmon.dt, SysMon icons, Desktop_Apps/AmixSysMon"
