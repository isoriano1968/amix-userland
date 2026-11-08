#!/bin/sh
# install.sh -- (on the A3000, as root, in a copy of cde-apps/amixq3 with
# cde-apps/panel beside it) put Quake III (amixq3) in place: the program, its
# CDE action and icons, and the control in the AMIX panel.  The game itself
# (z3660ARM_Core_q3.bin, c1q3 setuid root) comes from games/ioquake3.  Then
# Restart Workspace Manager; the Application Manager entry comes with the
# next login (or Application Manager, Reload Applications).
set -e
cp amixq3 /opt/amix/bin/amixq3.new
chmod 755 /opt/amix/bin/amixq3.new
mv /opt/amix/bin/amixq3.new /opt/amix/bin/amixq3
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C /etc/dt/appconfig/appmanager/C/Desktop_Apps; do
	[ -d $d ] || mkdir -p $d     # AMIX mkdir -p fails on existing ones
done
cp amixq3.dt /etc/dt/appconfig/types/C/
cp icons/AmixQ3.l.pm icons/AmixQ3.m.pm icons/AmixQ3.t.pm /etc/dt/appconfig/icons/C/
: > /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixQ3
chmod 755 /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixQ3
echo "installed: /opt/amix/bin/amixq3, amixq3.dt, icons, Desktop_Apps/AmixQ3"
[ -u /opt/amix/bin/c1q3 ] || echo "note: /opt/amix/bin/c1q3 (games/ioquake3, setuid root) is not installed yet"
(cd ../panel && sh install.sh)
