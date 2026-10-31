#!/bin/sh
# dtlogin-install.sh -- put the AMIX dtlogin config in place (run as root).
# /etc/dt/config is what dtlogin reads before /usr/dt/config.
T=/var/tmp
mkdir -p /etc/dt/config
cp $T/Xzz9000-dt /opt/amix/bin/Xzz9000-dt && chmod 755 /opt/amix/bin/Xzz9000-dt
cp /usr/dt/config/Xconfig /etc/dt/config/Xconfig
cp $T/Xservers /etc/dt/config/Xservers
chmod 644 /etc/dt/config/Xconfig /etc/dt/config/Xservers
rm -f $T/Xzz9000-dt $T/Xservers
ls -la /etc/dt/config /opt/amix/bin/Xzz9000-dt
