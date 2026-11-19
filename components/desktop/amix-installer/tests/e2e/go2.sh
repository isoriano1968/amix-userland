# go2.sh -- boot 2 of the end-to-end test, run by the boot hook as root (no
# login): the system after the install, dtlogin installed but off, turned on
# once for the greeter, then a root CDE session with startcde. Each "shot-*" put tells the host to take a screenshot;
# "e2e-done" ends the run. Log: /amixdata/e2e.log (sent with e2e-done).
H=10.0.2.2
L=/amixdata/e2e.log
put() {		# put FILE NAME
	/usr/bin/tftp $H 1069 <<E >/dev/null 2>&1
binary
put $1 $2
quit
E
}
mark() { echo "$1" > /tmp/e2e-mark; put /tmp/e2e-mark "$1"; }
exec >>$L 2>&1
echo "=== e2e boot 2: `/usr/bin/date`"
/usr/bin/uname -a
echo "battery clock: `/usr/amiga/bin/setclk -p`  clkzone: `/usr/bin/cat /etc/clkzone 2>&1`"
/usr/bin/ls -l /stand/unix /stand/unix.stock /dev/rtgfb
echo "amix-card: `/usr/bin/cat /etc/X11/amix-card 2>&1`"
/usr/bin/df -k / /amixdata
/usr/bin/pkg list 2>&1 | /usr/bin/head -60
# dtlogin: installed (desktop/cde-dtlogin) but off, so nothing started X at boot
echo "dtconfig -status: `/usr/dt/bin/dtconfig -status 2>&1`"
[ -f /etc/rc2.d/S99dtlogin ] && echo "FAIL: dtlogin is on at boot"
/usr/bin/ps -e | /usr/bin/grep Xamix >/dev/null && echo "FAIL: an X server runs before startcde"
# turned on once, as the README says: the greeter on the card
/usr/dt/bin/dtconfig -e
/etc/init.d/dtlogin start
i=0
until /usr/bin/ps -e | /usr/bin/grep Xamix >/dev/null; do
	i=`/usr/bin/expr $i + 1`
	[ $i -lt 30 ] || { echo "FAIL: no Xamix from dtlogin after 5 min"; break; }
	/usr/bin/sleep 10
done
/usr/bin/ps -ef | /usr/bin/egrep 'dtlogin|Xamix|dtgreet' | /usr/bin/grep -v grep
/usr/bin/sleep 90
mark shot-greeter
/usr/bin/sleep 30
/usr/bin/tail -30 /var/dt/Xamix.log
# and off again: CDE from the console with startcde (on the PATH)
/usr/dt/bin/dtconfig -kill
/usr/dt/bin/dtconfig -d
echo "dtconfig -status: `/usr/dt/bin/dtconfig -status 2>&1`"
/usr/bin/sleep 15
HOME=/amixdata/e2e-root; export HOME
[ -d $HOME ] || /usr/bin/mkdir -p $HOME
PATH=/usr/bin:/usr/sbin:/sbin; export PATH
echo "startcde: `type startcde`"
startcde > /amixdata/e2e-xinit.log 2>&1 &
/usr/bin/sleep 300
mark shot-session
# time for the host's Motif program on this display (E2E_CDE_SAMPLE)
/usr/bin/sleep 120
/usr/bin/ps -ef | /usr/bin/egrep 'Xamix|dtsession|dtwm|dtterm' | /usr/bin/grep -v grep
/usr/bin/ps -e | /usr/bin/grep dtsession >/dev/null || echo "FAIL: no dtsession from startcde"
/usr/bin/tail -20 /amixdata/e2e-xinit.log
/usr/bin/tail -20 $HOME/.dt/startlog 2>&1
echo "=== e2e boot 2 end: `/usr/bin/date`"
/usr/bin/sync
put $L e2e-done
