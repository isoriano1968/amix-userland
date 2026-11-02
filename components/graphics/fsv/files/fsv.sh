#!/bin/sh
# fsv -- start fsv with Mesa on the Z3660's core 1: the GPU program is loaded
# first if core 1 runs something else; without it fsv draws in software
C=/root/z3660ARMCores
# AMIX's default soft limits (16 MB data and memory) leave no room for libGL
ulimit -d unlimited
ulimit -v unlimited
if [ -x /usr/amiga/bin/ztop ] && /usr/amiga/bin/ztop | grep "ARM core 1" | grep -v " gpu," | grep -v " amix," > /dev/null; then
	if [ -x $C/armbench ] && [ -f $C/z3660ARM_Core_gpu.bin ]; then
		$C/armbench -reload $C/z3660ARM_Core_gpu.bin > /dev/null 2>&1 ||
			echo "fsv: could not load the GPU program on core 1; drawing in software" >&2
	fi
fi
exec /usr/local/libexec/fsv "$@"
