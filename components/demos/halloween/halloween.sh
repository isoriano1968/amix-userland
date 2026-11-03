#!/bin/sh
# halloween.sh -- the Halloween intro, on the Z3660's core 1; /opt/halloween
PATH=/usr/local/bin:$PATH; export PATH	# mikmod: not on the PATH of a CDE terminal
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " gpu," | grep -v " amix," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_amix.bin || exit 1
fi
cd /opt/halloween
exec ./halloween halloween_bass.it
