#!/bin/sh
# install-core1.sh -- (on the A3000, as root, in a directory holding the
# stage's z3660ARM_Core_wolf.bin and wolf.sh) put RtCW on core 1 in place;
# wolf.sh (the one launcher of the game) starts c1q3, which comes with
# games/ioquake3.  No backups (rebuild from git).  The paks are the user's,
# in /opt/rtcw/main.
set -e
cp z3660ARM_Core_wolf.bin /root/z3660ARMCores/z3660ARM_Core_wolf.bin.new
mv /root/z3660ARMCores/z3660ARM_Core_wolf.bin.new /root/z3660ARMCores/z3660ARM_Core_wolf.bin
cp wolf.sh /opt/rtcw/wolf.sh.new
chmod 755 /opt/rtcw/wolf.sh.new
mv /opt/rtcw/wolf.sh.new /opt/rtcw/wolf.sh
echo "installed: /root/z3660ARMCores/z3660ARM_Core_wolf.bin, /opt/rtcw/wolf.sh"
[ -u /opt/amix/bin/c1q3 ] || echo "note: /opt/amix/bin/c1q3 (games/ioquake3, setuid root) is not installed yet"
