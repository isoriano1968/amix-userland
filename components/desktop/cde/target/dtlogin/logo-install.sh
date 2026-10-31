#!/bin/sh
# logo-install.sh -- the AMIX logo on the dtlogin screen (run as root).
# Site files live under /etc/dt, which dtlogin reads before /usr/dt.
for d in /etc/dt/appconfig /etc/dt/appconfig/icons /etc/dt/appconfig/icons/C /etc/dt/config/C; do
	[ -d $d ] || mkdir $d
done
cp /var/tmp/AmixLogo.pm /etc/dt/appconfig/icons/C/AmixLogo.pm && chmod 644 /etc/dt/appconfig/icons/C/AmixLogo.pm
f=/etc/dt/config/C/Xresources
[ -f $f ] || cp /usr/dt/config/C/Xresources $f
if grep '^Dtlogin\*logo\*bitmapFile' $f > /dev/null; then
	:
else
	# "!" starts a comment only at the beginning of a line
	echo '!! AMIX: the AMIX logo instead of the CDE one' >> $f
	echo 'Dtlogin*logo*bitmapFile:	/etc/dt/appconfig/icons/C/AmixLogo.pm' >> $f
fi
chmod 644 $f
grep -n 'logo\*bitmapFile' $f
ls -la /etc/dt/appconfig/icons/C
rm -f /var/tmp/AmixLogo.pm /var/tmp/logo-install.sh
