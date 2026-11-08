#!/bin/sh
# wolf.sh [c1q3 options] [iortcw options] -- Return to Castle Wolfenstein
# (single player) with the whole engine and its GL on the Z3660's second ARM
# core (c1q3 with z3660ARM_Core_wolf.bin, games/iortcw): picture in the RTG
# overlay, sound on HDMI, the player's files in ~/.wolf.  The paks are your
# own (amix/trimpaks.py), in /opt/rtcw/main; the big one stays on AMIX and
# core 1 reads it by blocks.  F10 quits.  (The build with the engine on the
# 060 and GL through zzgl is wolf-zzgl.sh in the repo; not installed.)
[ $# -gt 0 ] || set -- +spmap escape1	# spmap: single player (devmap: no AI, no scripts)
exec /opt/amix/bin/c1q3 -core /root/z3660ARMCores/z3660ARM_Core_wolf.bin -base /opt/rtcw -game main \
	-home "$HOME/.wolf" +set com_zoneMegs 16 +set com_soundMegs 4 +set com_recommendedSet 1 \
	+set r_texturemode GL_LINEAR_MIPMAP_NEAREST "$@"
