#!/bin/sh
# install.sh -- (on the A3000, as root, in a copy of cde-apps/amixdisplay with
# cde-apps/panel beside it) put Display (amixdisplay) in place: the program,
# the setuid helper for the login screen's mode, its CDE files, the session
# and login-screen hooks, and the AMIX panel with its control.  Then Restart
# Workspace Manager; the Application Manager entry comes with the next login
# (or Application Manager, Reload Applications).
# A live change needs an Xzz9000 with RandR (x11/xzz9000 local-randr.patch);
# without it Display only saves the login screen's mode.
set -e
cp amixdisplay /opt/amix/bin/amixdisplay.new
chmod 755 /opt/amix/bin/amixdisplay.new
mv /opt/amix/bin/amixdisplay.new /opt/amix/bin/amixdisplay
[ -d /opt/amix/lib/amixdisplay ] || mkdir -p /opt/amix/lib/amixdisplay   # AMIX mkdir -p fails on existing ones
cp setlogin /opt/amix/lib/amixdisplay/setlogin.new
/usr/bin/chown root /opt/amix/lib/amixdisplay/setlogin.new
/usr/bin/chgrp sys /opt/amix/lib/amixdisplay/setlogin.new
chmod 4755 /opt/amix/lib/amixdisplay/setlogin.new
mv /opt/amix/lib/amixdisplay/setlogin.new /opt/amix/lib/amixdisplay/setlogin
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C /etc/dt/appconfig/appmanager/C/Desktop_Apps \
	 /etc/dt/config/Xsession.d; do
	[ -d $d ] || mkdir -p $d
done
cp amixdisplay.dt /etc/dt/appconfig/types/C/
cp icons/Display.l.pm icons/Display.m.pm icons/Display.t.pm /etc/dt/appconfig/icons/C/
: > /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixDisplay
chmod 755 /etc/dt/appconfig/appmanager/C/Desktop_Apps/AmixDisplay
# the user's own mode at login, before dtwm starts
cp 0012.amixdisplay /etc/dt/config/Xsession.d/ && chmod 755 /etc/dt/config/Xsession.d/0012.amixdisplay
# the login screen's mode again before each greeter (after a user's session
# left the server in another mode); dtlogin runs Xsetup as root, and waits
x=/etc/dt/config/Xsetup
if [ ! -f $x ]; then
	sed 's%XDIR=/usr/bin%XDIR=/usr/X/bin%' /usr/dt/config/Xsetup > $x
	chmod 755 $x
fi
if grep amixdisplay $x > /dev/null; then
	:
else
	echo '[ -x /opt/amix/bin/amixdisplay ] && /opt/amix/bin/amixdisplay -system	# AMIX: the login screen'"'"'s screen mode' >> $x
fi
echo "installed: /opt/amix/bin/amixdisplay, /opt/amix/lib/amixdisplay/setlogin (setuid root),"
echo "  amixdisplay.dt, Display icons, Desktop_Apps/AmixDisplay, Xsession.d/0012.amixdisplay, Xsetup hook"
# every CDE application of ours is in the AMIX subpanel (panel/amix.fp)
(cd ../panel && sh install.sh)
