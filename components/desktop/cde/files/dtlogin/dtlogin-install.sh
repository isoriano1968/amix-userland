#!/bin/sh
# dtlogin-install.sh -- put the AMIX dtlogin config in place (run as root).
# /etc/dt/config is what dtlogin reads before /usr/dt/config.
# The server is Xamix (x11/xserver-amix, /usr/x11r6/bin/Xamix); install it
# first.  The wrapper was Xzz9000-dt before 2026-11-16: Xservers no longer
# names it, so it goes.
T=/var/tmp
[ -x /usr/x11r6/bin/Xamix ] || { echo "no /usr/x11r6/bin/Xamix: install x11/xserver-amix first" >&2; exit 1; }
mkdir -p /etc/dt/config
cp $T/Xamix-dt /opt/amix/bin/Xamix-dt.new && chmod 755 /opt/amix/bin/Xamix-dt.new && \
	mv /opt/amix/bin/Xamix-dt.new /opt/amix/bin/Xamix-dt
cp /usr/dt/config/Xconfig /etc/dt/config/Xconfig
cp $T/Xservers /etc/dt/config/Xservers
chmod 644 /etc/dt/config/Xconfig /etc/dt/config/Xservers
rm -f /opt/amix/bin/Xzz9000-dt
rm -f $T/Xamix-dt $T/Xservers
ls -la /etc/dt/config /opt/amix/bin/Xamix-dt
