#!/bin/sh
# amixcast -- the streaming window; loads the amix core (the player, GL and
# crypto) onto core 1 when a core without the player runs there.
# /opt/amix/bin/amixcast, the binary in /opt/vplay.
C=/root/z3660ARMCores
if /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " h264bench," | grep -v " amix," >/dev/null; then
	$C/armbench -reload $C/z3660ARM_Core_amix.bin >/dev/null 2>&1
fi
exec /opt/vplay/amixcast "$@"
