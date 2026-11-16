# go2.sh -- boot 2 of the end-to-end test, run by the boot hook as root (no
# login): the system after the install, the dtlogin greeter, then a root CDE
# session through xinit. Each "shot-*" put tells the host to take a screenshot;
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
# the greeter: dtlogin (S99dtlogin, before this hook) starts Xamix-dt
i=0
until /usr/bin/ps -e | /usr/bin/grep Xamix >/dev/null; do
	i=`/usr/bin/expr $i + 1`
	[ $i -lt 30 ] || { echo "no Xamix after 5 min"; break; }
	/usr/bin/sleep 10
done
/usr/bin/ps -ef | /usr/bin/egrep 'dtlogin|Xamix|dtgreet' | /usr/bin/grep -v grep
/usr/bin/sleep 90
mark shot-greeter
/usr/bin/sleep 30
/usr/bin/tail -30 /var/dt/Xamix.log
# a root CDE session without a password: dtlogin off, then xinit with CDE's
# xinitrc (as /usr/dt/bin/startcde with STARTX=xinit)
/etc/init.d/dtlogin stop
/usr/bin/sleep 15
HOME=/amixdata/e2e-root; export HOME
[ -d $HOME ] || /usr/bin/mkdir -p $HOME
PATH=/usr/dt/bin:/usr/x11r6/bin:/usr/X/bin:/usr/bin:/usr/sbin:/sbin; export PATH
X=/usr/x11r6/bin/xinit
[ -x $X ] || X=/usr/X/bin/xinit
echo "xinit: $X"
$X /usr/dt/config/xinitrc.amix -- /usr/x11r6/bin/Xamix :0 -fp /usr/X/lib/fonts/misc/,/usr/X/lib/fonts/Xol/,/usr/X/lib/fonts/75dpi/,/usr/X/lib/fonts/100dpi/,/usr/dt/config/xfonts/C/ \
	-co /usr/X/lib/rgb -kb > /amixdata/e2e-xinit.log 2>&1 &
/usr/bin/sleep 300
mark shot-session
/usr/bin/sleep 30
/usr/bin/ps -ef | /usr/bin/egrep 'Xamix|dtsession|dtwm|dtterm' | /usr/bin/grep -v grep
/usr/bin/tail -20 /amixdata/e2e-xinit.log
/usr/bin/tail -20 $HOME/.dt/startlog 2>&1
echo "=== e2e boot 2 end: `/usr/bin/date`"
/usr/bin/sync
put $L e2e-done
