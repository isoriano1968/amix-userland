#!/bin/sh
# AMIX-INSTALL.sh -- finish installing CDE on AMIX after the tree is unpacked:
#
#     cd / && tar xf /tmp/cde-amix.tar && sh /usr/dt/AMIX-INSTALL.sh
#
# Run as root.  It only touches /usr/dt, /etc/dt and /var/dt.  It does NOT
# edit /etc/inetd.conf or /etc/services; the optional network services are
# printed at the end for you to add by hand.

[ "`id | sed 's/^uid=\([0-9]*\).*/\1/'`" = 0 ] || { echo "run as root"; exit 1; }
[ -x /usr/dt/bin/dtsession ] || { echo "/usr/dt is not unpacked"; exit 1; }

echo "== ownership and modes"
chown -R bin /usr/dt; chgrp -R bin /usr/dt
# as CDE's install databases have them
chown root /usr/dt/bin/dtaction /usr/dt/bin/dtappgather /usr/dt/bin/dtprintinfo \
	/usr/dt/bin/dtsession /usr/dt/bin/dtterm
chmod 6555 /usr/dt/bin/dtaction
chmod 4555 /usr/dt/bin/dtappgather /usr/dt/bin/dtprintinfo /usr/dt/bin/dtsession \
	/usr/dt/bin/dtterm
chgrp mail /usr/dt/bin/dtmail /usr/dt/bin/dtmailpr && chmod 2555 /usr/dt/bin/dtmail /usr/dt/bin/dtmailpr

echo "== /etc/dt and /var/dt"
for d in /etc/dt /etc/dt/config /etc/dt/appconfig /etc/dt/appconfig/types \
	/etc/dt/appconfig/types/C /etc/dt/appconfig/icons /etc/dt/appconfig/icons/C \
	/var/dt /var/dt/appconfig /var/dt/appconfig/appmanager; do
	[ -d $d ] || mkdir $d
	chmod 755 $d
done
[ -d /var/dt/tmp ] || mkdir /var/dt/tmp
chmod 1777 /var/dt/tmp

echo "== ToolTalk types (tt_type_comp)"
cd /usr/dt/appconfig/tttypes/src || exit 1
rm -f types.xdr .tt_lock
for p in dtfile dticon dtpad dtcm dtmail dtdbcache dtterm dtinfo; do
	TTPATH=. /usr/dt/bin/tt_type_comp -sd user -m $p.ptype || echo "   $p.ptype FAILED"
	rm -f .tt_lock
done
if [ -f types.xdr ]; then
	mv types.xdr ../types.xdr && chmod 444 ../types.xdr && echo "   /usr/dt/appconfig/tttypes/types.xdr"
fi

echo "== terminfo for dtterm (TERM=dtterm, 8 ANSI colours)"
# AMIX's curses reads /usr/share/lib/terminfo, which has no dtterm entry
(cd /var/tmp && tic /usr/dt/config/dtterm.ti) && echo "   `tput -T dtterm colors` colours"

cat <<'EOF'

== done.  To try it:   startx /usr/dt/bin/Xsession   (or startx-z3 with an
   .xinitrc that execs /usr/dt/bin/Xsession).  Session log: $HOME/.dt/startlog

== optional network services (NOT added -- edit by hand if you want them):
   /etc/services:    dtspc  6112/tcp
   /etc/inetd.conf:  dtspc stream tcp nowait root /usr/dt/bin/dtspcd /usr/dt/bin/dtspcd
                     100083/1 stream rpc/tcp wait root /usr/dt/bin/rpc.ttdbserver rpc.ttdbserver
                     100068/2-5 dgram rpc/udp wait root /usr/dt/bin/rpc.cmsd rpc.cmsd
   (dtspcd: remote actions; ttdbserver: ToolTalk file database; cmsd: dtcm)
EOF
