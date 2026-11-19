#!/bin/sh
# e2e-fsuae.sh CORE.hdf REPO WORKDIR [PROFILE.json] -- the installer from end to
# end in FS-UAE, headless: an A3000 with its 68030 and a Picasso II.
#
#   1. amix-installer --apply writes the disks from a COPY of CORE into
#      WORKDIR/out (core, pvmk, data disk, the rtgfb kernel, the FS-UAE config);
#   2. hookfile.py puts a boot hook into that copy's /etc/rc2.d/S99zz9000
#      (`tftp get go; sh go`: no login, no password typed);
#   3. boot 1: FS-UAE under Xvfb (never the user's display), the installer's
#      --net session serves go and the steps; the foundation report and the
#      log land in WORKDIR; screenshots every few minutes;
#   4. boot 2: the new kernel; go2.sh is served as go (greeter, root CDE).
# Kills only its own pids; WORKDIR keeps the logs and screenshots, and the
# disk copies until "e2e-fsuae.sh --clean WORKDIR".
# FS-UAE runs at the A3000's own speed: the install is network-bound, and at
# maximum CPU speed (E2E_FAST=1) slirp gets less time, so every step is slower.
# E2E_AGENT=1: after boot 1, vscode-agent's tests/amix-e2e.js with the key
# the installer wrote (a profile with the VS Code agent on).
# E2E_BOOT2_ONLY=1 reuses WORKDIR's disks after a finished boot 1:
# boot 2 again, with go2.sh (or WORKDIR/go2) as it is now.
# Needs: Xvfb ($XVFB or on PATH), fs-uae, xwd, python3 with GTK not needed;
# for the Picasso II kernel AMIX_RTGFB_KIT and m68k-cbm-sysv4-ld on PATH.
set -e
# never the user's display: only FS-UAE gets one, the private Xvfb's
unset DISPLAY WAYLAND_DISPLAY
HERE=$(cd "$(dirname "$0")" && pwd)
APP=$(cd "$HERE/../.." && pwd)
if [ "$1" = --clean ]; then
	[ -d "$2/out" ] && rm -r "$2/out"
	echo "e2e: disk copies removed from $2"
	exit 0
fi
CORE=${1:?usage: e2e-fsuae.sh CORE.hdf REPO WORKDIR [PROFILE]}
REPO=${2:?repo}
W=${3:?workdir}
PROFILE=$4
DISP=${E2E_DISPLAY:-:93}
XVFB=${XVFB:-Xvfb}
mkdir -p "$W/shots" "$W/out"
W=$(cd "$W" && pwd)
FAST=
[ -z "$E2E_FAST" ] || FAST="--uae_cpu_speed=max --uae_cpu_cycle_exact=0 --uae_cycle_exact=0"
export AMIX_E2E_DIR=$W
# one FS-UAE end-to-end run at a time on this computer (ports 7777, 1069,
# 8069; vscode-agent's tests/run-fsuae.sh takes the same lock): exit 75 = busy
LOCK=${AMIX_E2E_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/amix-fsuae-e2e.lock}
exec 9>"$LOCK"
flock -n 9 || { echo "e2e: another FS-UAE end-to-end run holds $LOCK"; exit 75; }
# the curses screen check (amix-e2e.js): console-hello with AMIX's own curses,
# VS Code's terminal engine; with the terminfo package the agent's TERM
if [ -z "$AMIX_E2E_CURSES" ] && command -v m68k-elf-gcc >/dev/null; then
	sh "$HERE/curses-hello.sh" "$W/hello-svr4" && export AMIX_E2E_CURSES="svr4=$W/hello-svr4"
fi
AGENT_TESTS=$APP/../../developer/vscode-agent/tests
[ -d "$AGENT_TESTS/node_modules/@xterm/headless" ] || [ ! -f "$AGENT_TESTS/package.json" ] ||
	(cd "$AGENT_TESTS" && npm install --silent)
ls "$REPO"/publisher/amix/pkg/sysutils%2Fterminfo >/dev/null 2>&1 &&
	export AMIX_E2E_TERM=${AMIX_E2E_TERM:-xterm-256color}
PIDS=
cleanup() { [ -z "$PIDS" ] || kill $PIDS 2>/dev/null || true; }
trap cleanup EXIT INT TERM

CONF=$W/out/amix-install.fs-uae
if [ -n "$E2E_BOOT2_ONLY" ]; then
	[ -f "$CONF" ] || { echo "e2e: no finished boot 1 in $W"; exit 1; }
	$XVFB "$DISP" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	sleep 2
fi
if [ -n "$E2E_BOOT2_ONLY" ]; then :; elif [ -z "$PROFILE" ]; then
	PROFILE=$W/profile.json
	(cd "$APP" && python3 -c '
import sys
from amix_installer import model, profile
c = model.Config(sel={"src": "hdf", "target": "file", "mach": "uae", "accel": "none",
                      "kern": "stock", "boot": "pvmk", "gfx": "picasso2", "desk": "cde",
                      "layout": "split", "dsize": "d1g", "net": "net2065", "keymap": "de"},
                 on={k: False for k in model.DEFAULT_ON})
c.on["pkg"] = True
c.normalize()
profile.save(c, sys.argv[1])
' "$PROFILE")
fi
shot() {
	win=$(DISPLAY=$DISP xwininfo -root -tree | awk '/FS-UAE/ {print $1; exit}')
	[ -n "$win" ] || return 0
	DISPLAY=$DISP xwd -id "$win" -silent > "$W/s.xwd" &&
		python3 "$HERE/xwd2png.py" "$W/s.xwd" "$W/shots/$1.png" >/dev/null && rm -f "$W/s.xwd"
}
fsuae() {
	DISPLAY=$DISP SDL_AUDIODRIVER=dummy fs-uae "$CONF" --automatic_input_grab=0 $FAST \
		--window_width=1100 --window_height=860 > "$W/fsuae-$1.log" 2>&1 &
	FSPID=$!
	PIDS="$PIDS $FSPID"
}
if [ -z "$E2E_BOOT2_ONLY" ]; then
echo "e2e: profile $PROFILE"

# 1. the disks
(cd "$APP" && ./amix-installer --profile "$PROFILE" --source "$CORE" --out "$W/out" \
	--repo "$REPO" --no-fs-uae-list --apply) > "$W/apply.log" 2>&1 || {
	echo "e2e: --apply failed"; tail -20 "$W/apply.log"; exit 1; }
DISK=$(awk -F' = ' '/^hard_drive_[0-9] = .*-core\.hdf$/ {print $2}' "$CONF")
[ -f "$DISK" ] || { echo "e2e: no core disk in $CONF"; exit 1; }

# 2. the boot hook
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
python3 "$HERE/hookfile.py" "$DISK" /etc/rc2.d/S99zz9000 "$W/hook.sh"

# 3. boot 1
$XVFB "$DISP" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
PIDS="$PIDS $!"
sleep 2
(cd "$APP" && PYTHONUNBUFFERED=1 exec ./amix-installer --profile "$PROFILE" --source "$CORE" \
	--out "$W/out" --repo "$REPO" --no-fs-uae-list --net) > "$W/net.log" 2>&1 &
NETPID=$!
PIDS="$PIDS $NETPID"
fsuae 1
t=0
until grep -qE "amix: (DONE|FAILED)" "$W/net.log"; do
	sleep 30; t=$((t + 30))
	[ $((t % 300)) -ne 0 ] || shot "boot1-$(printf %05d $t)"
	kill -0 $NETPID 2>/dev/null || break
	# the steps never started (bootstrap dead, no network): stop, do not wait hours
	[ $t -lt 900 ] || grep -q "amix: step" "$W/net.log" || { shot boot1-nostart; echo "e2e: no step after 15 min"; tail -5 "$W/net.log"; exit 1; }
	[ $t -lt ${E2E_TIMEOUT:-10800} ] || { echo "e2e: boot 1 timed out"; break; }
done
sleep 10
shot boot1-end
cp "$W/out/foundation-report.txt" "$W/" 2>/dev/null || true
grep -q "amix: DONE" "$W/net.log" || { echo "e2e: boot 1 did not finish"; tail -30 "$W/net.log"; exit 1; }
python3 "$HERE/clockcheck.py" "$W/net.log" || { echo "e2e: the AMIX clock is off"; exit 1; }
# E2E_AGENT=1 (a profile with the VS Code agent): its own checks, AMIX still up
KEY=$(cat "$W/out/vscode-agent.key" 2>/dev/null || true)
U=$(sed -n 's/.*VS Code agent: runs programs as \([^,]*\), dials.*/\1/p' "$W/net.log" | tail -1)
if [ -n "$E2E_AGENT" ]; then
	[ -n "$KEY" ] && [ -n "$U" ] || { echo "e2e: no agent key or user"; exit 1; }
	E2E_TIMEOUT_MIN=${E2E_TIMEOUT_MIN:-15} node "$APP/../../developer/vscode-agent/tests/amix-e2e.js" \
		"$KEY" 7777 "$U" > "$W/agent-e2e.log" 2>&1 || true
	tail -3 "$W/agent-e2e.log"
	grep -q "all passed" "$W/agent-e2e.log" || { echo "e2e: the agent checks FAILED"; exit 1; }
	echo "e2e: the VS Code agent checks passed"
fi
kill $FSPID $NETPID 2>/dev/null || true
wait $FSPID $NETPID 2>/dev/null || true
echo "e2e: boot 1 DONE after ${t}s"
fi

# 4. boot 2: the new kernel; go2.sh (or WORKDIR/go2) is served as go
[ -z "$E2E_BOOT1_ONLY" ] || { echo "e2e: stopping after boot 1"; exit 0; }
G2=$HERE/go2.sh
[ ! -f "$W/go2" ] || G2=$W/go2
rm -rf "$W/tftp"
mkdir -p "$W/tftp"
cp "$G2" "$W/tftp/go"
python3 -u "$HERE/tftpd.py" "$W/tftp" > "$W/tftpd.log" 2>&1 &
PIDS="$PIDS $!"
fsuae 2
t=0
rc=0
CDE_DONE=
until grep -q "put e2e-done" "$W/tftpd.log"; do
	sleep 20; t=$((t + 20))
	[ $((t % 120)) -ne 0 ] || shot "boot2-$(printf %05d $t)"
	[ $t -lt ${E2E_TIMEOUT2:-3600} ] || { echo "e2e: boot 2 timed out"; break; }
	for m in $(ls "$W/tftp/put" 2>/dev/null | grep '^shot-' || true); do
		[ -f "$W/shots/boot2-$m.png" ] || shot "boot2-$m"
	done
	# E2E_CDE_SAMPLE=BINARY: once CDE runs, a Motif program through the VS Code
	# agent on AMIX's own display (as Run on Target does), its window shot
	if [ -n "$E2E_CDE_SAMPLE" ] && [ -z "$CDE_DONE" ] && [ -f "$W/tftp/put/shot-session" ]; then
		CDE_DONE=1
		node "$APP/../../developer/vscode-agent/tests/run-on-target.js" "$KEY" 7777 "$U" \
			"$E2E_CDE_SAMPLE" --env DISPLAY=:0 --keep 45 --wait 5 > "$W/cde-sample.log" 2>&1 &
		CPID=$!
		PIDS="$PIDS $CPID"
		sleep 30
		shot boot2-cde-sample
		if wait $CPID; then echo "e2e: the CDE sample ran on AMIX's display"
		else echo "e2e: the CDE sample FAILED"; tail -8 "$W/cde-sample.log"; rc=1; fi
	fi
done
shot boot2-end
echo "e2e: boot 2 finished after ${t}s; screenshots in $W/shots"
grep -q "put e2e-done" "$W/tftpd.log" || rc=1
# what AMIX found wrong in boot 2 (go2.sh: "FAIL: ..." lines in its log)
if grep "^FAIL" "$W/tftp/put/e2e-done" 2>/dev/null; then rc=1; fi
[ -z "$E2E_CDE_SAMPLE" ] || [ -n "$CDE_DONE" ] || { echo "e2e: CDE never came up for the sample"; rc=1; }
exit $rc
