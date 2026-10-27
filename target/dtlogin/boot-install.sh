#!/bin/sh
# boot-install.sh -- dtlogin at boot (dtconfig), the Motif/TWM/OpenLook
# sessions; run as root with the files in /var/tmp/dtboot
T=/var/tmp/dtboot
cp $T/dtlogin.rc /etc/init.d/dtlogin && chmod 744 /etc/init.d/dtlogin
cp $T/dtconfig /usr/dt/bin/dtconfig && chmod 555 /usr/dt/bin/dtconfig
[ -d /opt/amix/lib ] || mkdir /opt/amix/lib
[ -d /opt/amix/lib/dt ] || mkdir /opt/amix/lib/dt
cp $T/Xsession.wm /opt/amix/lib/dt/Xsession.wm && chmod 755 /opt/amix/lib/dt/Xsession.wm
[ -d /etc/dt/config/C/Xresources.d ] || mkdir /etc/dt/config/C/Xresources.d
cp $T/Xresources.mwm $T/Xresources.twm $T/Xresources.olwm /etc/dt/config/C/Xresources.d/
chmod 644 /etc/dt/config/C/Xresources.d/*
ls -la /etc/init.d/dtlogin /usr/dt/bin/dtconfig /opt/amix/lib/dt /etc/dt/config/C/Xresources.d
ls -la /usr/x11r6/lib/X11/twm 2>&1 | head -3
cd / && rm -rf $T
