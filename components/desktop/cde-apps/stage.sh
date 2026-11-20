#!/bin/sh
# stage.sh DIR [base|z3660|amixuae|amixq3] -- the apps as the install.sh
# scripts place them on the box, under DIR, for one IPS package:
#   base     desktop/cde-apps: the AMIX control and subpanel, System
#            Monitor, Display, Printers, Software Center
#   z3660    desktop/cde-z3660: the Z3660 window (ztopcde) and AmixCast
#   amixuae  desktop/cde-amixuae: the Amiga emulator's window (amixuae)
#   amixq3   desktop/cde-amixq3: Quake III / RtCW (amixq3)
# Each app brings its own control for the AMIX subpanel (<app>.fp), its
# action, icons and Application Manager entry.  The core 1 packages put
# theirs in /etc/dt/appconfig-hw/z3660-core1 (types, icons, appmanager),
# which the session searches only when amix-hw finds the Z3660.
# Owners and setuid bits are not set here: the manifests give them
# (mode=4755 group=sys ...), and pkg applies them.  Left out: amixdisplay's
# hook in /etc/dt/config/Xsetup (a file of the system's, which a package
# cannot edit; install.sh adds it on the box).
set -e
cd "$(dirname "$0")"
D=${1:?usage: stage.sh DIR [base|z3660|amixuae|amixq3]}
WHAT=${2:-base}
B=$D/opt/amix/bin
case $WHAT in
base)	C=$D/etc/dt/appconfig ;;
z3660|amixuae|amixq3)
	C=$D/etc/dt/appconfig-hw/z3660-core1 ;;
*)	echo "stage.sh: $WHAT: base, z3660, amixuae or amixq3" >&2; exit 2 ;;
esac
T=$C/types/C
I=$C/icons/C
A=$C/appmanager/C/Desktop_Apps
mkdir -p $B $T $I $A

app() {		# app NAME DTFILE ICON...: the program, its action, panel control and icons
	n=$1 dt=$2; shift 2
	cp $n/$n $B/$n
	cp $n/$dt $n/$n.fp $T/
	for i in "$@"; do cp $n/icons/$i.l.pm $n/icons/$i.m.pm $n/icons/$i.t.pm $I/; done
}
entry() {	# entry NAME: an Application Manager entry (an empty executable)
	: > $A/$1
	chmod 755 $A/$1
}

case $WHAT in
base)
	mkdir -p $D/etc/dt/config/Xsession.d $D/opt/amix/lib/amixdisplay \
		$D/opt/amix/lib/amixprinters $D/opt/amix/lib/amixsoftware

	app amixdisplay amixdisplay.dt Display
	cp amixdisplay/setlogin $D/opt/amix/lib/amixdisplay/setlogin
	cp amixdisplay/0012.amixdisplay $D/etc/dt/config/Xsession.d/
	chmod 755 $D/etc/dt/config/Xsession.d/0012.amixdisplay
	entry AmixDisplay

	app amixprinters amixprinters.dt AmixPrinters
	for t in amixprfind prsend prset; do cp amixprinters/$t $D/opt/amix/lib/amixprinters/$t; done
	cp amixprinters/testpage.ps $D/opt/amix/lib/amixprinters/
	entry AmixPrinters

	app amixsoftware amixsoftware.dt AmixSoftware
	cp amixsoftware/swrun $D/opt/amix/lib/amixsoftware/swrun
	entry AmixSoftware

	app amixsysmon amixsysmon.dt SysMon
	entry AmixSysMon

	# the AMIX control and its (empty) subpanel
	cp panel/amix.fp $T/
	cp panel/AmixPanel.l.pm panel/AmixPanel.m.pm panel/AmixPanel.t.pm $I/
	;;
z3660)
	# amixcast: the program in /opt/vplay, its launcher in /opt/amix/bin
	mkdir -p $D/opt/vplay
	cp amixcast/amixcast $D/opt/vplay/amixcast
	cp amixcast/amixcast.sh $B/amixcast
	cp amixcast/amixcast.dt amixcast/amixcast.fp $T/
	cp amixcast/icons/AmixCast.l.pm amixcast/icons/AmixCast.m.pm amixcast/icons/AmixCast.t.pm $I/
	entry AmixCast

	app ztopcde ztopcde.dt Z3660
	entry ZtopCDE
	;;
amixuae)
	cp amixuae/amixuae $B/amixuae
	cp amixuae/amixuae.dt amixuae/amixuae.fp $T/
	cp amixuae/icons/*.pm $I/		# AmixUAE and the types AmigaADF, AmigaHDF, AmigaROM
	entry AmixUAE
	;;
amixq3)
	app amixq3 amixq3.dt AmixQ3
	entry AmixQ3
	;;
esac

chmod -R u+w,go-w,go+rX $D
