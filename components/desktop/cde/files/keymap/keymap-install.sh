#!/bin/sh
# keymap-install.sh -- X keyboard layout from the console's keymap (run as root)
T=/var/tmp/keymap
cp $T/xmodmap.amiga-* /usr/x11r6/lib/X11/ && chmod 644 /usr/x11r6/lib/X11/xmodmap.amiga-*
cp $T/xkeymap-amiga /opt/amix/bin/ && chmod 755 /opt/amix/bin/xkeymap-amiga
# CDE sessions
[ -d /etc/dt/config/Xsession.d ] || mkdir /etc/dt/config/Xsession.d
cp $T/0015.amix-keymap /etc/dt/config/Xsession.d/ && chmod 755 /etc/dt/config/Xsession.d/0015.amix-keymap
# the dtlogin screen: its own Xsetup (the shipped one looks in /usr/bin for xset)
if [ ! -f /etc/dt/config/Xsetup ]; then
	sed 's%XDIR=/usr/bin%XDIR=/usr/X/bin%' /usr/dt/config/Xsetup > /etc/dt/config/Xsetup
	echo '[ -x /opt/amix/bin/xkeymap-amiga ] && /opt/amix/bin/xkeymap-amiga	# AMIX: console keyboard layout' >> /etc/dt/config/Xsetup
	chmod 755 /etc/dt/config/Xsetup
fi
# startx-rtg sessions (mwm, twm, ...): after "export DISPLAY"
f=/opt/amix/bin/startx-rtg
if grep xkeymap-amiga $f > /dev/null; then
	:
else
	cp -p $f $f.pre-keymap
	awk '{print} /^export DISPLAY$/ && !done {print "[ -x /opt/amix/bin/xkeymap-amiga ] && /opt/amix/bin/xkeymap-amiga\t# the console keyboard layout"; done=1}' $f.pre-keymap > $f.new && [ -s $f.new ] && cat $f.new > $f
	rm -f $f.new
fi
grep -n xkeymap $f /etc/dt/config/Xsetup /etc/dt/config/Xsession.d/*
rm -rf $T
