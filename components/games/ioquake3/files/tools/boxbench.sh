#!/bin/sh
# boxbench.sh q3|wolf LABEL [extra engine args...] -- one frozen-view
# benchmark of the core 1 game image on the A3000, through amix-box (every
# box step; it waits while another session holds the box, and the run
# itself counts as a player): devmap, a fixed view, the world frozen
# (timescale 0.001), then the last 150 frames of com_speeds (core 1's own
# clock) averaged.  Shows on the box's screen for ~4-6 minutes; leaves
# nothing in /var/tmp and core 1 back on the amix image.
#   files/tools/boxbench.sh q3 base
#   files/tools/boxbench.sh wolf base
# Results append to build/bench.txt.
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
game=$1 label=$2
shift 2
BOX=$top/../../../tools/amix-box
K="-i $HOME/.ssh/id_ed25519_amix -o BatchMode=yes -o IdentitiesOnly=yes"
R=root@192.168.0.133
case $game in
q3)	core=/root/z3660ARMCores/z3660ARM_Core_q3.bin base=/opt/ioq3 dir=demoq3 seed=/opt/ioq3/home/qkey
	sched='-exec 30 "devmap q3dm1" -exec 110 "setviewpos 1052 1432 24 135" -exec 112 "timescale 0.001" -exec 151 c1prof -exec 199 c1prof -exec 200 quit'
	args="" ;;
wolf)	core=/root/z3660ARMCores/z3660ARM_Core_wolf.bin base=/opt/rtcw dir=main seed=
	sched='-exec 30 "devmap escape1" -exec 170 "timescale 0.001" -exec 221 c1prof -exec 269 c1prof -exec 270 quit'
	args="+set com_zoneMegs 16 +set com_soundMegs 4 +set com_recommendedSet 1" ;;
*)	echo "usage: boxbench.sh q3|wolf LABEL [engine args]"; exit 2 ;;
esac
tmp=$(mktemp -d)
cat > "$tmp/bb.sh" <<EOF
#!/bin/sh
# boxbench $game ($label): a frozen view, then quit
DISPLAY=:0; XAUTHORITY=/home/isoriano/.Xauthority; export DISPLAY XAUTHORITY
mkdir /var/tmp/bbhome
${seed:+cp $seed /var/tmp/bbhome/}
/usr/bin/chown isoriano /var/tmp/bbhome
/opt/amix/bin/c1q3 -v -core $core -base $base -game $dir -home /var/tmp/bbhome $sched \\
	$args +set com_speeds 1 +set cg_drawFPS 1 $* < /dev/null > /var/tmp/bb.out 2> /var/tmp/bb.log
echo "exit \$?" >> /var/tmp/bb.out
EOF
$BOX -w 1800 -m "boxbench $game $label: copy script" scp -q $K "$tmp/bb.sh" $R:/var/tmp/bb.sh
$BOX -w 1800 -m "boxbench $game $label: start" ssh $K $R '(nohup sh /var/tmp/bb.sh < /dev/null > /dev/null 2>&1 &)'
sleep 90
while $BOX -s | grep -q "a player runs"; do sleep 20; done
$BOX -w 1800 -m "boxbench $game $label: results" ssh $K $R 'cat /var/tmp/bb.out /var/tmp/bb.log' > "$tmp/log"
$BOX -w 1800 -m "boxbench $game $label: clean up" ssh $K $R '/usr/gnu/bin/find /var/tmp/bbhome -type f -exec rm {} \; ;
	/usr/gnu/bin/find /var/tmp/bbhome -depth -type d -exec rmdir {} \; ;
	rm /var/tmp/bb.sh /var/tmp/bb.out /var/tmp/bb.log;
	/opt/amix/bin/armbench -reload /root/z3660ARMCores/z3660ARM_Core_amix.bin > /dev/null; ls -a /var/tmp'
grep "^exit\|CL_InitCGame\|sp_fast refused\|sp_fast_prof\|FAILED\|fault" "$tmp/log" || true
res=$(grep "^frame:" "$tmp/log" | tail -150 | sed 's/: */:/g' | awk '{for(i=2;i<=NF;i++){split($i,a,":"); s[a[1]]+=a[2]} n++}
	END {if (n) printf "frames %d all %.1f bk %.1f cl %.1f gm %.1f", n, s["all"]/n, s["bk"]/n, s["cl"]/n, s["gm"]/n}')
mkdir -p "$top/build"
echo "$(date '+%F %T') $game $label: $res" | tee -a "$top/build/bench.txt"
rm -f "$tmp/bb.sh" "$tmp/log"
rmdir "$tmp"
