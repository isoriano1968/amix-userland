#!/bin/sh
# agent-only-fsuae.sh CORE.hdf PVMK.hdf REPO WORKDIR -- the VS Code agent alone
# into a running AMIX, from end to end in FS-UAE, headless (Xvfb, never the
# user's display): an A3000 with its 68030 and an A2065 on slirp.
#
#   1. copies of the two disks in WORKDIR; a boot hook in the core copy's
#      /etc/rc2.d/S99zz9000 runs what the user types at the console
#      (`tftp 10.0.2.2 1069`, get go, `sh go`);
#   2. amix-installer --running fs-uae --agent-only --net, with HOME in
#      WORKDIR: it serves go, the bootstrap and REPO, and AMIX installs
#      developer/vscode-agent and nothing else; the key lands in
#      WORKDIR/home/.local/share/amix-installer/vscode-agent.key;
#   3. the AMIX clock within 5 s of this computer's (clockcheck.py); with
#      E2E_MOTIF=1 the "Agent and Motif" install, libXm and libX11 on the disk;
#   4. vscode-agent's tests/amix-e2e.js with that key: the agent dials in and
#      its checks run (programs as the user the installer reported).
# Passes when the installer exits 0 on DONE, the clock is right and amix-e2e.js
# says "all passed".
# Kills only its own pids; "agent-only-fsuae.sh --clean WORKDIR" removes the
# disk copies.
set -e
# never the user's display: only FS-UAE gets one, the private Xvfb's
unset DISPLAY WAYLAND_DISPLAY
HERE=$(cd "$(dirname "$0")" && pwd)
APP=$(cd "$HERE/../.." && pwd)
AGENT=$(cd "$APP/../../developer/vscode-agent" && pwd)
if [ "$1" = --clean ]; then
	rm -f "$2/core.hdf" "$2/pvmk.hdf"
	echo "agent-only: disk copies removed from $2"
	exit 0
fi
CORE=${1:?usage: agent-only-fsuae.sh CORE.hdf PVMK.hdf REPO WORKDIR}
PVMK=${2:?pvmk.hdf}
REPO=$(cd "${3:?repo}" && pwd)
W=${4:?workdir}
DISP=${E2E_DISPLAY:-:95}
XVFB=${XVFB:-Xvfb}
KICK=${KICKSTART:-$HOME/Documents/FS-UAE/Kickstarts/A3000.47.115.rom}
mkdir -p "$W/shots" "$W/home"
W=$(cd "$W" && pwd)
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

cp "$CORE" "$W/core.hdf"
cp "$PVMK" "$W/pvmk.hdf"
cat > "$W/hook.sh" <<'EOF'
#!/sbin/sh
# boot hook (disposable copy): what the user types as root at the console
cd /tmp
/usr/bin/tftp 10.0.2.2 1069 <<E >/dev/null 2>&1
binary
get go /tmp/go
quit
E
[ -s /tmp/go ] && /sbin/sh /tmp/go > /tmp/go.out 2>&1
EOF
python3 "$HERE/hookfile.py" "$W/core.hdf" /etc/rc2.d/S99zz9000 "$W/hook.sh"

MOTIF=
[ -z "$E2E_MOTIF" ] || MOTIF=--with-motif
HOME="$W/home" python3 -u "$APP/amix-installer" --running fs-uae --agent-only $MOTIF --net \
	--repo "$REPO" > "$W/installer.log" 2>&1 &
IPID=$!
PIDS="$PIDS $IPID"
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
		python3 "$HERE/xwd2png.py" "$W/s.xwd" "$W/shots/$1.png" >/dev/null && rm -f "$W/s.xwd"
}
DISPLAY=$DISP SDL_AUDIODRIVER=dummy fs-uae "$W/agent.fs-uae" --automatic_input_grab=0 \
	--window_width=1100 --window_height=860 > "$W/fsuae.log" 2>&1 &
FSPID=$!
PIDS="$PIDS $FSPID"
( n=0; while kill -0 $FSPID 2>/dev/null; do sleep 60; n=$((n + 1)); shot "t$(printf %03d $n)"; done ) &
PIDS="$PIDS $!"

rc=0
wait $IPID || rc=$?
echo "agent-only: the installer exited $rc"
# the clock: setclk from the repository, the time from this computer to the second
python3 "$HERE/clockcheck.py" "$W/installer.log" || rc=1
# E2E_MOTIF=1 (Agent and Motif): the Motif and X11 run time on AMIX's disk
if [ -n "$E2E_MOTIF" ]; then
	sync
	python3 "$HERE/hasfile.py" "$W/core.hdf" /usr/motif/lib/libXm.so.4 \
		/usr/motif/lib/libMrm.so.4 /usr/x11r6/lib/libX11.so.7 || rc=1
fi
KEY=$(cat "$W/home/.local/share/amix-installer/vscode-agent.key" 2>/dev/null || true)
U=$(sed -n 's/.*VS Code agent: runs programs as \([^,]*\), dials.*/\1/p' "$W/installer.log" | tail -1)
if [ $rc = 0 ] && [ -n "$KEY" ] && [ -n "$U" ]; then
	E2E_TIMEOUT_MIN=${E2E_TIMEOUT_MIN:-10} node "$AGENT/tests/amix-e2e.js" "$KEY" 7777 "$U" |
		tee "$W/e2e.log" || rc=1
	grep -q "all passed" "$W/e2e.log" || rc=1
else
	rc=1
fi
shot end
echo "agent-only: $( [ $rc = 0 ] && echo passed || echo FAILED ); logs $W/installer.log, $W/e2e.log, screenshots $W/shots"
exit $rc
