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

echo "== session environment (/etc/dt/config/sys.dtprofile)"
# New users' ~/.dtprofile is copied from this file at their first CDE login;
# existing ones are left alone (append the AMIX block to them by hand).
if [ -f /etc/dt/config/sys.dtprofile ]; then
	echo "   exists, left as it is"
else
	cp /usr/dt/config/sys.dtprofile /etc/dt/config/sys.dtprofile
	cat >> /etc/dt/config/sys.dtprofile <<'E'

# AMIX: the system's tools in every session (dtterm, the front panel, actions)
PATH=$PATH:/usr/ucb:/usr/X/bin:/usr/amiga/bin
[ -d /opt/amix/bin ] && PATH=$PATH:/opt/amix/bin
export PATH
# AMIX: X11R6.3's shared libraries before the system's X11R5 ones
if [ -d /usr/x11r6/lib/r63 ]; then
	LD_LIBRARY_PATH=/usr/x11r6/lib/r63${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
	export LD_LIBRARY_PATH
fi
# AMIX: bash in terminals, if installed (dtterm runs $SHELL)
[ -x /usr/local/bin/bash ] && { SHELL=/usr/local/bin/bash; export SHELL; }
E
	chmod 444 /etc/dt/config/sys.dtprofile
	echo "   written"
fi

echo "== X11R6.3 runtime links (/usr/x11r6/lib/r63)"
# The versioned R6.3 shared libraries in a directory of their own, so that
# LD_LIBRARY_PATH can put them first without the R6.3 build tree's unversioned
# names (and without hiding AMIX's X11R5 libraries from the programs built for it).
if [ -d /usr/x11r6/xc/exports/lib ]; then
	[ -d /usr/x11r6/lib ] || mkdir -p /usr/x11r6/lib
	[ -d /usr/x11r6/lib/r63 ] || mkdir /usr/x11r6/lib/r63
	for l in /usr/x11r6/xc/exports/lib/lib*.so.*; do
		[ -f "$l" ] || continue
		n=`basename $l`
		[ -h /usr/x11r6/lib/r63/$n ] || ln -s $l /usr/x11r6/lib/r63/$n
	done
	echo "   `ls /usr/x11r6/lib/r63 | wc -l` libraries"
else
	echo "   no /usr/x11r6/xc/exports/lib: skipped"
fi

echo "== site resources (/etc/dt/config/C/sys.resources)"
[ -d /etc/dt/config/C ] || mkdir /etc/dt/config/C
if [ -f /etc/dt/config/C/sys.resources ]; then
	echo "   exists, left as it is"
else
	cat > /etc/dt/config/C/sys.resources <<'E'
!! /etc/dt/config/C/sys.resources -- AMIX site resources, loaded by dtsession
!! after /usr/dt/config/C/sys.resources.
!! Show dotfiles (.profile, .bashrc, ...) in file dialogs; the Dt app-defaults
!! hide them (XmFILTER_HIDDEN_FILES).
*XmFileSelectionBox.fileFilterStyle:	XmFILTER_NONE
E
	chmod 444 /etc/dt/config/C/sys.resources
	echo "   written"
fi

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

== done.  To try it:   startcde   (or startx /usr/dt/bin/Xsession).
   Session log: $HOME/.dt/startlog
   Users who already have a ~/.dtprofile: append the "AMIX:" lines at the end
   of /etc/dt/config/sys.dtprofile to it (PATH, R6.3 libraries, bash).

== optional network services (NOT added -- edit by hand if you want them):
   /etc/services:    dtspc  6112/tcp
   /etc/inetd.conf:  dtspc stream tcp nowait root /usr/dt/bin/dtspcd /usr/dt/bin/dtspcd
                     100083/1 stream rpc/tcp wait root /usr/dt/bin/rpc.ttdbserver rpc.ttdbserver
                     100068/2-5 dgram rpc/udp wait root /usr/dt/bin/rpc.cmsd rpc.cmsd
   (dtspcd: remote actions; ttdbserver: ToolTalk file database; cmsd: dtcm)
EOF
