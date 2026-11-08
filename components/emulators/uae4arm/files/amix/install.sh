#!/bin/sh
# install.sh -- (on the A3000, as root, in a directory holding the stage's
# files: z3660ARM_Core_uae.bin, c1uae, c1bench.adf, coremark.060,
# coremark.000) put the emulator in place.  No backups (rebuild from git).
# Needs armbench in /opt/amix/bin (amix-gate firmware/core1) and a
# ZZ9K_MAP_BOARD kernel.  The front end is desktop/cde-apps/amixuae.
set -e
for d in /root/z3660ARMCores /opt/amix/bin /opt/uae/bench; do
	[ -d $d ] || mkdir -p $d     # AMIX mkdir -p fails on existing ones
done
cp z3660ARM_Core_uae.bin /root/z3660ARMCores/z3660ARM_Core_uae.bin.new
mv /root/z3660ARMCores/z3660ARM_Core_uae.bin.new /root/z3660ARMCores/z3660ARM_Core_uae.bin
cp c1uae /opt/amix/bin/c1uae.new
/usr/bin/chown root /opt/amix/bin/c1uae.new
/usr/bin/chgrp sys /opt/amix/bin/c1uae.new
chmod 4755 /opt/amix/bin/c1uae.new     # the board is root's; it drops root before opening files
mv /opt/amix/bin/c1uae.new /opt/amix/bin/c1uae
cp c1bench.adf coremark.060 coremark.000 /opt/uae/bench/
chmod 755 /opt/uae/bench/coremark.060 /opt/uae/bench/coremark.000
echo "installed: /root/z3660ARMCores/z3660ARM_Core_uae.bin, /opt/amix/bin/c1uae (setuid root), /opt/uae/bench"
