#!/bin/sh
# install-c1q3.sh -- (on the A3000, as root, in a directory holding the
# stage's z3660ARM_Core_q3.bin, c1q3 and q3.sh) put Quake III on core 1 in
# place; q3.sh (the one launcher of the game) starts c1q3.
# No backups (rebuild from git).  Needs armbench in /opt/amix/bin (amix-gate
# firmware/core1), a ZZ9K_MAP_BOARD kernel and the game data in /opt/ioq3.
set -e
for d in /root/z3660ARMCores /opt/amix/bin; do
	[ -d $d ] || mkdir -p $d     # AMIX mkdir -p fails on existing ones
done
cp z3660ARM_Core_q3.bin /root/z3660ARMCores/z3660ARM_Core_q3.bin.new
mv /root/z3660ARMCores/z3660ARM_Core_q3.bin.new /root/z3660ARMCores/z3660ARM_Core_q3.bin
cp c1q3 /opt/amix/bin/c1q3.new
/usr/bin/chown root /opt/amix/bin/c1q3.new
/usr/bin/chgrp sys /opt/amix/bin/c1q3.new
chmod 4755 /opt/amix/bin/c1q3.new     # the board is root's; it drops root before the display and files
mv /opt/amix/bin/c1q3.new /opt/amix/bin/c1q3
if [ -f q3.sh ]; then
	cp q3.sh /opt/ioq3/q3.sh.new
	chmod 755 /opt/ioq3/q3.sh.new
	mv /opt/ioq3/q3.sh.new /opt/ioq3/q3.sh
fi
echo "installed: /root/z3660ARMCores/z3660ARM_Core_q3.bin, /opt/amix/bin/c1q3 (setuid root), /opt/ioq3/q3.sh"
