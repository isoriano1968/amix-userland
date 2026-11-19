#!/bin/sh
# nodata-fsuae.sh CORE.hdf PVMK.hdf REPO WORKDIR -- a running AMIX in FS-UAE
# whose configuration has no data disk, from end to end, headless (Xvfb):
#
#   A. the user's own configuration: core and pvmk only.  amix-installer
#      --running fs-uae --fs-uae-config CONF --net with Python and vim
#      chosen (about 87 MB): more than the stock root holds.  The room check must stop it before
#      anything changes: FAILED "not enough room", the bootstrap removed
#      again, Commodore's setclk still in place;
#   B. amix-installer --fs-uae-config CONF --add-data-disk 512m adds an empty
#      data disk on SCSI ID 1 (the old configuration kept), FS-UAE starts
#      again, and the base set installs: step 4 makes the data disk's file
#      system, mounts it on /amixdata and moves /usr/local and /opt there.
# Passes when A stops as it should and B ends in DONE with /amixdata in
# /etc/vfstab.  Kills only its own pids; one FS-UAE run at a time (the lock).
set -e
# never the user's display: only FS-UAE gets one, the private Xvfb's
unset DISPLAY WAYLAND_DISPLAY
HERE=$(cd "$(dirname "$0")" && pwd)
APP=$(cd "$HERE/../.." && pwd)
if [ "$1" = --clean ]; then
	rm -f "$2/core.hdf" "$2/pvmk.hdf" "$2"/*-data.hdf
	echo "nodata: disk copies removed from $2"
	exit 0
fi
CORE=${1:?usage: nodata-fsuae.sh CORE.hdf PVMK.hdf REPO WORKDIR}
PVMK=${2:?pvmk.hdf}
REPO=$(cd "${3:?repo}" && pwd)
W=${4:?workdir}
DISP=${E2E_DISPLAY:-:96}
XVFB=${XVFB:-Xvfb}
KICK=${KICKSTART:-$HOME/Documents/FS-UAE/Kickstarts/A3000.47.115.rom}
mkdir -p "$W/shots" "$W/home"
W=$(cd "$W" && pwd)
export AMIX_E2E_DIR=$W
LOCK=${AMIX_E2E_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/amix-fsuae-e2e.lock}
exec 9>"$LOCK"
flock -n 9 || { echo "nodata: another FS-UAE end-to-end run holds $LOCK"; exit 75; }
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
CONF=$W/user.fs-uae
cat > "$CONF" <<EOF
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
# A: Python and vim; B: the base set
(cd "$APP" && python3 -c '
import sys
from amix_installer import model, profile
big = sys.argv[1] == "big"
on = dict((k, False) for k in model.DEFAULT_ON)
on.update(pkg=True, hw_a2065=True, python=big, vim=big)
c = model.Config(sel={"src": "run", "mach": "uae", "accel": "none", "desk": "console", "gfx": "chipset",
                      "net": "net2065", "keymap": "de"}, on=on)
assert c.sel["src"] == "run" and c.on["python"] == big, (c.sel, c.on)
profile.save(c, sys.argv[2])
' big "$W/big.json" && python3 -c '
import sys
from amix_installer import model, profile
on = dict((k, False) for k in model.DEFAULT_ON)
on.update(pkg=True, hw_a2065=True)
c = model.Config(sel={"src": "run", "mach": "uae", "accel": "none", "desk": "console", "gfx": "chipset", "net": "net2065",
                      "keymap": "de"}, on=on)
assert c.sel["src"] == "run", c.sel
profile.save(c, sys.argv[1])
' "$W/base.json")

$XVFB "$DISP" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
PIDS="$PIDS $!"
sleep 2
shot() {
	win=$(DISPLAY=$DISP xwininfo -root -tree | awk '/FS-UAE/ {print $1; exit}')
	[ -n "$win" ] || return 0
	DISPLAY=$DISP xwd -id "$win" -silent > "$W/s.xwd" &&
		python3 "$HERE/xwd2png.py" "$W/s.xwd" "$W/shots/$1.png" >/dev/null && rm -f "$W/s.xwd"
}
boot() {    # boot NAME PROFILE [installer options]: one FS-UAE boot with the install
	name=$1 prof=$2
	shift 2
	HOME="$W/home" python3 -u "$APP/amix-installer" --profile "$prof" --running fs-uae \
		--fs-uae-config "$CONF" --repo "$REPO" "$@" --net > "$W/$name.log" 2>&1 &
	IPID=$!
	DISPLAY=$DISP SDL_AUDIODRIVER=dummy fs-uae "$CONF" --automatic_input_grab=0 \
		--window_width=1100 --window_height=860 > "$W/fsuae-$name.log" 2>&1 &
	FSPID=$!
	PIDS="$PIDS $IPID $FSPID"
	rc=0
	wait $IPID || rc=$?
	shot "$name-end"
	kill $FSPID 2>/dev/null || true
	wait $FSPID 2>/dev/null || true
	sync
	return $rc
}

rc=0
# --- A ---------------------------------------------------------------------
if boot a "$W/big.json"; then
	echo "nodata: A: the install did not stop"; rc=1
fi
grep -q "amix: FAILED: not enough room: no data disk" "$W/a.log" ||
	{ echo "nodata: A: no room failure in a.log"; tail -5 "$W/a.log"; rc=1; }
grep "amix: room:" "$W/a.log" || true
grep -q "amix: foundation" "$W/a.log" && { echo "nodata: A: the foundation ran"; rc=1; }
grep -q "Give it a data disk" "$W/a.log" || { echo "nodata: A: no advice"; rc=1; }
# nothing installed: no bootstrap, no package client, Commodore's setclk
python3 "$HERE/hasfile.py" "$W/core.hdf" /opt/amixboot/bin/pkg /usr/bin/pkg 2>/dev/null |
	grep -q present && { echo "nodata: A: the bootstrap or pkg is still on the disk"; rc=1; }
[ $rc = 0 ] && echo "nodata: A: stopped before any change, as it should"

# --- B ---------------------------------------------------------------------
python3 "$APP/amix-installer" --fs-uae-config "$CONF" --add-data-disk 512m > "$W/add.log" 2>&1 ||
	{ echo "nodata: B: --add-data-disk failed"; cat "$W/add.log"; exit 1; }
cat "$W/add.log"
grep -q "hard_drive_2_controller = scsi1" "$CONF" || { echo "nodata: B: no data disk line"; rc=1; }
[ -f "$CONF.before-amix-installer" ] || { echo "nodata: B: old configuration not kept"; rc=1; }
if boot b "$W/base.json"; then :; else echo "nodata: B: the install failed"; tail -8 "$W/b.log"; rc=1; fi
grep "amix: room:\|amix: step 4\|data disk\|/amixdata\|amix: DONE" "$W/b.log" | cut -c1-150
grep -q "amix: step 4: the data disk on /dev/rdsk/c1d0s1" "$W/b.log" || { echo "nodata: B: no step 4"; rc=1; }
grep -q "amix: DONE" "$W/b.log" || rc=1
python3 "$HERE/hasfile.py" "$W/core.hdf" /usr/bin/pkg /usr/amiga/bin/setclk || rc=1
python3 - "$W/core.hdf" "$APP" <<'EOF' || rc=1
import os, sys
sys.path.insert(0, sys.argv[2])
from amix_installer import hdf
with hdf.Image(sys.argv[1]) as im:
    im.read_rdb()
    fs = hdf.Ufs(im, [p for p in im.partitions if p.kind == "amix-root"][0])
    vfstab = fs.read_file("/etc/vfstab").decode("latin-1")
ok = "/amixdata" in vfstab
print("nodata: B: /etc/vfstab %s /amixdata" % ("has" if ok else "LACKS"))
sys.exit(0 if ok else 1)
EOF
echo "nodata: $( [ $rc = 0 ] && echo passed || echo FAILED ); logs $W/a.log, $W/b.log, screenshots $W/shots"
exit $rc
