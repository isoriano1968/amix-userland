#!/bin/sh
# amixcast -- the streaming window; loads the player onto core 1 when another
# program runs there.  /opt/amix/bin/amixcast, the binary in /opt/vplay.
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " h264bench," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_h264bench.bin >/dev/null 2>&1
fi
exec /opt/vplay/amixcast "$@"
