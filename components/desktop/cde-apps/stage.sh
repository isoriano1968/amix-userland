#!/bin/sh
# stage.sh DIR -- the apps as the install.sh scripts place them on the box,
# under DIR, for the IPS package. Owners and setuid bits are not set here:
# cde-apps.manifest gives them (mode=4755 group=sys ...), and pkg applies them.
# Left out: amixdisplay's hook in /etc/dt/config/Xsetup (a file of the
# system's, which a package cannot edit; install.sh adds it on the box).
set -e
cd "$(dirname "$0")"
D=${1:?usage: stage.sh DIR}
B=$D/opt/amix/bin
T=$D/etc/dt/appconfig/types/C
I=$D/etc/dt/appconfig/icons/C
A=$D/etc/dt/appconfig/appmanager/C/Desktop_Apps
mkdir -p $B $T $I $A $D/opt/vplay $D/etc/dt/config/Xsession.d \
	$D/opt/amix/lib/amixdisplay $D/opt/amix/lib/amixprinters $D/opt/amix/lib/amixsoftware

app() {		# app NAME DTFILE ICON...: the program, its action and icons
	n=$1 dt=$2; shift 2
	cp $n/$n $B/$n
	cp $n/$dt $T/
	for i in "$@"; do cp $n/icons/$i.l.pm $n/icons/$i.m.pm $n/icons/$i.t.pm $I/; done
}
entry() {	# entry NAME: an Application Manager entry (an empty executable)
	: > $A/$1
	chmod 755 $A/$1
}

# amixcast: the program in /opt/vplay, its launcher in /opt/amix/bin
cp amixcast/amixcast $D/opt/vplay/amixcast
cp amixcast/amixcast.sh $B/amixcast
cp amixcast/amixcast.dt $T/
cp amixcast/icons/AmixCast.l.pm amixcast/icons/AmixCast.m.pm amixcast/icons/AmixCast.t.pm $I/

app amixdisplay amixdisplay.dt Display
cp amixdisplay/setlogin $D/opt/amix/lib/amixdisplay/setlogin
cp amixdisplay/0012.amixdisplay $D/etc/dt/config/Xsession.d/
chmod 755 $D/etc/dt/config/Xsession.d/0012.amixdisplay
entry AmixDisplay

app amixprinters amixprinters.dt AmixPrinters
for t in amixprfind prsend prset; do cp amixprinters/$t $D/opt/amix/lib/amixprinters/$t; done
cp amixprinters/testpage.ps $D/opt/amix/lib/amixprinters/
entry AmixPrinters

app amixq3 amixq3.dt AmixQ3
entry AmixQ3

app amixsoftware amixsoftware.dt AmixSoftware
cp amixsoftware/swrun $D/opt/amix/lib/amixsoftware/swrun
entry AmixSoftware

app amixsysmon amixsysmon.dt SysMon
entry AmixSysMon

cp amixuae/amixuae $B/amixuae
cp amixuae/amixuae.dt $T/
cp amixuae/icons/*.pm $I/		# AmixUAE and the types AmigaADF, AmigaHDF, AmigaROM
entry AmixUAE

app ztopcde ztopcde.dt Z3660

# the AMIX front panel
cp panel/amix.fp $T/
cp panel/AmixPanel.l.pm panel/AmixPanel.m.pm panel/AmixPanel.t.pm $I/

chmod -R u+w,go-w,go+rX $D
