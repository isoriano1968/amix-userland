#!/bin/sh
# run-fsuae.sh CORE.hdf PVMK.hdf WORKDIR -- vscode-agent on AMIX in FS-UAE,
# headless (Xvfb, never the user's display): an A3000 with its 68030 and an
# A2065 on slirp.
#   1. copies of the two disks in WORKDIR; a boot hook in the core copy's
#      /etc/rc2.d/S99zz9000 (`tftp get go; sh go`, the installer's e2e hook);
#   2. go, as root at boot: the agent (libamixc99 linked in), its rc script and a
#      configuration (user guest, a fresh key, connect 10.0.2.2:PORT), then
#      /etc/init.d/vscode-agent start;
#   3. tests/amix-e2e.js waits for the agent to dial in and runs its checks.
# Kills only its own pids; "run-fsuae.sh --clean WORKDIR" removes the copies.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
COMP=$(cd "$HERE/.." && pwd)
E2E=$(cd "$COMP/../../desktop/amix-installer/tests/e2e" && pwd)
if [ "$1" = --clean ]; then
	rm -f "$2/core.hdf" "$2/pvmk.hdf"
	echo "run-fsuae: disk copies removed from $2"
	exit 0
fi
CORE=${1:?usage: run-fsuae.sh CORE.hdf PVMK.hdf WORKDIR}
PVMK=${2:?pvmk.hdf}
W=${3:?workdir}
PORT=${PORT:-7777}
DISP=${E2E_DISPLAY:-:94}
XVFB=${XVFB:-Xvfb}
SYSROOT=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}
KICK=${KICKSTART:-$HOME/Documents/FS-UAE/Kickstarts/A3000.47.115.rom}
mkdir -p "$W/tftp" "$W/shots"
W=$(cd "$W" && pwd)
export AMIX_E2E_DIR=$W
PIDS=
cleanup() { [ -z "$PIDS" ] || kill $PIDS 2>/dev/null || true; }
trap cleanup EXIT INT TERM

[ -f "$COMP/build/vscode-agent" ] || { echo "run-fsuae: make build first"; exit 1; }
cp "$CORE" "$W/core.hdf"
cp "$PVMK" "$W/pvmk.hdf"
KEY=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")

cat > "$W/hook.sh" <<'EOF'
#!/sbin/sh
# e2e boot hook (disposable copy): fetch go from the host, run it
cd /tmp
/usr/bin/tftp 10.0.2.2 1069 <<E >/dev/null 2>&1
binary
get go /tmp/go
quit
E
[ -s /tmp/go ] && /sbin/sh /tmp/go
EOF
python3 "$E2E/hookfile.py" "$W/core.hdf" /etc/rc2.d/S99zz9000 "$W/hook.sh"

cp "$COMP/build/vscode-agent" "$COMP/files/vscode-agent.rc" "$W/tftp/"
printf 'connect 10.0.2.2:%s\nuser guest\nsecret %s\nlog /var/log/vscode-agent.log\nretry 5\n' \
	"$PORT" "$KEY" > "$W/tftp/vscode-agent.conf"
cat > "$W/tftp/go" <<'EOF'
H=10.0.2.2
get() {
	/usr/bin/tftp $H 1069 <<E >/dev/null 2>&1
binary
get $1 $2
quit
E
}
put() {
	/usr/bin/tftp $H 1069 <<E >/dev/null 2>&1
binary
put $1 $2
quit
E
}
exec > /tmp/go.log 2>&1
echo "=== vscode-agent e2e: `/usr/bin/date`"
/usr/bin/uname -a
/usr/bin/df -k /
[ -d /usr/local/bin ] || mkdir /usr/local/bin
get vscode-agent /usr/local/bin/vscode-agent
chmod 755 /usr/local/bin/vscode-agent
get vscode-agent.rc /etc/init.d/vscode-agent
chmod 744 /etc/init.d/vscode-agent
get vscode-agent.conf /etc/vscode-agent.conf
chown root /etc/vscode-agent.conf
chmod 600 /etc/vscode-agent.conf
/usr/bin/ls -l /usr/local/bin/vscode-agent /etc/vscode-agent.conf; /usr/bin/ls /usr/lib/libamixc99.so.1 2>/dev/null || echo 'no libamixc99.so.1 on this disk'
/usr/local/bin/vscode-agent -V
/etc/init.d/vscode-agent start
echo "rc start: $?"
/usr/bin/sleep 3
/usr/bin/ps -ef | /usr/bin/grep vscode-agent | /usr/bin/grep -v grep
put /tmp/go.log go.log
EOF

python3 -u "$E2E/tftpd.py" "$W/tftp" > "$W/tftpd.log" 2>&1 &
PIDS="$PIDS $!"
$XVFB "$DISP" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
PIDS="$PIDS $!"
sleep 2
cat > "$W/agent.fs-uae" <<EOF
[fs-uae]
amiga_model = A3000
chip_memory = 2048
motherboard_ram = 16384
kickstart_file = $KICK
network_card = a2065
hard_drive_0 = $W/pvmk.hdf
hard_drive_0_controller = scsi5
hard_drive_0_type = rdb
hard_drive_1 = $W/core.hdf
hard_drive_1_controller = scsi6
hard_drive_1_type = rdb
EOF
shot() {
	win=$(DISPLAY=$DISP xwininfo -root -tree | awk '/FS-UAE/ {print $1; exit}')
	[ -n "$win" ] || return 0
	DISPLAY=$DISP xwd -id "$win" -silent > "$W/s.xwd" &&
		python3 "$E2E/xwd2png.py" "$W/s.xwd" "$W/shots/$1.png" >/dev/null && rm -f "$W/s.xwd"
}
DISPLAY=$DISP SDL_AUDIODRIVER=dummy fs-uae "$W/agent.fs-uae" --automatic_input_grab=0 \
	--window_width=1100 --window_height=860 > "$W/fsuae.log" 2>&1 &
FSPID=$!
PIDS="$PIDS $FSPID"
( n=0; while kill -0 $FSPID 2>/dev/null; do sleep 60; n=$((n + 1)); shot "t$(printf %03d $n)"; done ) &
PIDS="$PIDS $!"
rc=0
E2E_TIMEOUT_MIN=${E2E_TIMEOUT_MIN:-30} node "$HERE/amix-e2e.js" "$KEY" "$PORT" guest | tee "$W/e2e.log" || rc=1
grep -q "all passed" "$W/e2e.log" || rc=1
shot end
cp "$W/tftp/put/go.log" "$W/" 2>/dev/null || true
echo "run-fsuae: $( [ $rc = 0 ] && echo passed || echo FAILED ); log $W/e2e.log, screenshots $W/shots"
exit $rc
