#!/bin/sh
# install.sh -- (on the A3000, as root, in this directory) put AmixCast in place;
# then Reload Actions.  The PC runs amixcast.py.
set -e
[ -d /opt/vplay ] || mkdir /opt/vplay
cp amixcast /opt/vplay/amixcast
cp amixcast.sh /opt/amix/bin/amixcast
chmod 755 /opt/vplay/amixcast /opt/amix/bin/amixcast
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C; do [ -d $d ] || mkdir -p $d; done
cp amixcast.dt /etc/dt/appconfig/types/C/
cp icons/AmixCast.l.pm icons/AmixCast.m.pm icons/AmixCast.t.pm /etc/dt/appconfig/icons/C/
echo "installed: /opt/amix/bin/amixcast, amixcast.dt, icons"
