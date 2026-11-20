#!/bin/sh
# amixsh-fsuae.sh CORE.hdf PVMK.hdf WORKDIR SCRIPT... -- every SCRIPT checked
# by AMIX's own /sbin/sh, in FS-UAE, headless (Xvfb), in a few minutes:
#   sh -n SCRIPT       the syntax, as AMIX's sh reads it;
#   WORKDIR/cases      one shell line per case (optional): each run alone
#                      with sh, its output and exit status reported.
# The host's sh (dash, bash) accepts constructs AMIX's SVR4 sh rejects
# ("bad substitution", "!: not found"); tests/test_bourne.py lints for the
# known ones, this is the real check.  A COPY of CORE gets a boot hook
# (hookfile.py); results come back by TFTP to WORKDIR/tftp/put/out.
# Kills only its own pids; one FS-UAE run at a time (the lock).
set -e
unset DISPLAY WAYLAND_DISPLAY
HERE=$(cd "$(dirname "$0")" && pwd)
CORE=${1:?usage: amixsh-fsuae.sh CORE.hdf PVMK.hdf WORKDIR SCRIPT...}
PVMK=${2:?pvmk.hdf}
W=${3:?workdir}
shift 3
DISP=${E2E_DISPLAY:-:95}
XVFB=${XVFB:-Xvfb}
KICK=${KICKSTART:-$HOME/Documents/FS-UAE/Kickstarts/A3000.47.115.rom}
mkdir -p "$W/tftp"
W=$(cd "$W" && pwd)
export AMIX_E2E_DIR=$W
LOCK=${AMIX_E2E_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/amix-fsuae-e2e.lock}
exec 9>"$LOCK"
flock -n 9 || { echo "amixsh: another FS-UAE end-to-end run holds $LOCK"; exit 75; }
PIDS=
cleanup() { [ -z "$PIDS" ] || kill $PIDS 2>/dev/null || true; }
trap cleanup EXIT INT TERM

rm -rf "$W/tftp"/* && mkdir -p "$W/tftp/put"
names=
for f in "$@"; do
	n=$(basename "$f")
	cp "$f" "$W/tftp/$n"
	names="$names $n"
done
[ -f "$W/cases" ] && cp "$W/cases" "$W/tftp/cases" || : > "$W/tftp/cases"
cat > "$W/tftp/run.sh" <<EOF
cd /tmp
for n in $names cases; do
	/usr/bin/tftp 10.0.2.2 1069 <<E >/dev/null 2>&1
binary
get \$n /tmp/amixsh-\$n
quit
E
done
for n in $names; do
	echo "== sh -n \$n"
	/sbin/sh -n /tmp/amixsh-\$n 2>&1
	echo "status \$?"
done
i=0
while read line; do
	i=\`expr \$i + 1\`
	echo "\$line" > /tmp/amixsh-case
	echo "== case \$i: \$line"
	/sbin/sh /tmp/amixsh-case 2>&1
	echo "status \$?"
done < /tmp/amixsh-cases
EOF
cat > "$W/hook.sh" <<'EOF'
#!/sbin/sh
# boot hook (disposable copy): fetch run.sh, run it, send the output back
cd /tmp
/usr/bin/tftp 10.0.2.2 1069 <<E >/dev/null 2>&1
binary
get run.sh /tmp/amixsh-run
quit
E
/sbin/sh /tmp/amixsh-run > /tmp/amixsh-out 2>&1
/usr/bin/tftp 10.0.2.2 1069 <<E >/dev/null 2>&1
binary
put /tmp/amixsh-out out
quit
E
/usr/bin/rm -f /tmp/amixsh-*
EOF
cp "$CORE" "$W/core.hdf"
cp "$PVMK" "$W/pvmk.hdf"
python3 "$HERE/hookfile.py" "$W/core.hdf" /etc/rc2.d/S99zz9000 "$W/hook.sh"
cat > "$W/sh.fs-uae" <<EOF
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
python3 -u "$HERE/tftpd.py" "$W/tftp" > "$W/tftpd.log" 2>&1 &
PIDS="$PIDS $!"
$XVFB "$DISP" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
PIDS="$PIDS $!"
sleep 2
DISPLAY=$DISP SDL_AUDIODRIVER=dummy fs-uae "$W/sh.fs-uae" --automatic_input_grab=0 \
	> "$W/fsuae.log" 2>&1 &
PIDS="$PIDS $!"
t=0
until grep -q "put out" "$W/tftpd.log"; do
	sleep 10; t=$((t + 10))
	[ $t -lt ${AMIXSH_TIMEOUT:-900} ] || { echo "amixsh: no result after ${t}s"; exit 1; }
done
sleep 3
cat "$W/tftp/put/out"
rm -f "$W/core.hdf" "$W/pvmk.hdf"
