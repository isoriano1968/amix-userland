#!/bin/sh
# q3.sh [c1q3 options] [ioquake3 options] -- Quake III Arena (the demo) with
# the whole engine and its GL on the Z3660's second ARM core (c1q3,
# z3660ARM_Core_q3.bin): picture in the RTG overlay over the window, sound
# on HDMI, the player's files in ~/.q3a.  F10 quits.  Core 1 runs only the
# game meanwhile (no GL, H.264 or crypto offload for others).  The CDE front
# end is Quake III in the AMIX panel (amixq3).  (The build with the engine
# on the 060 and GL through zzgl is q3-zzgl.sh in the repo; not installed.)
exec /opt/amix/bin/c1q3 -base /opt/ioq3 -game demoq3 "$@"
