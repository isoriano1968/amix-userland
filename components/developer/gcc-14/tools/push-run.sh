#!/bin/bash
# push-run.sh -- move a binary onto the AMIX machine over TFTP and run it.
#
# TFTP because it needs nothing installed on either side: AMIX ships
# /usr/bin/tftp, and tools/tftp_onesock.py is a read-only server that answers
# from its listening port.  The machine pulls; we never write to its filesystem
# except into the directory named below.
#
# Usage: push-run.sh <local-binary> [arguments-to-pass]
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "push-run.sh: no config.sh" >&2; exit 1; }
. ./config.sh
BIN="$1"; shift || true
[ -f "$BIN" ] || { echo "push-run.sh: no such file: $BIN" >&2; exit 1; }
NAME=$(basename "$BIN")
PORT="${TFTP_PORT:-1069}"
: "${HOST_ADDR:?set HOST_ADDR in config.sh -- find it with: ip route get \$AMIX_HOST}"

# A dedicated directory, so a second session working on the same machine is
# never surprised by our files -- and under /root rather than /tmp, because
# this machine crashes and /tmp does not survive a reboot.  A probe is small
# enough that re-pushing costs little, but the directory should match where the
# larger payloads live so there is one place to look.
REMOTE_DIR=/root/amix-probe

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"; [ -n "$TFTP_PID" ] && kill "$TFTP_PID" 2>/dev/null' EXIT
cp "$BIN" "$STAGE/$NAME"

python3 tools/tftp_onesock.py "$STAGE" >"$STAGE/tftp.log" 2>&1 &
TFTP_PID=$!
sleep 1
kill -0 "$TFTP_PID" 2>/dev/null || { echo "push-run.sh: TFTP server did not start:"; cat "$STAGE/tftp.log"; exit 1; }

python3 tools/hw.py --timeout 120 \
  "mkdir -p $REMOTE_DIR; cd $REMOTE_DIR && rm -f $NAME && (echo binary; echo 'get $NAME'; echo quit) | tftp $HOST_ADDR $PORT >/dev/null 2>&1; ls -l $NAME" \
  "cd $REMOTE_DIR && chmod 755 $NAME && ./$NAME $*; echo \"exit status: \$?\""

echo "--- tftp server log ---"
cat "$STAGE/tftp.log"
