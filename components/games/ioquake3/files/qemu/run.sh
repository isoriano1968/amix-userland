#!/bin/sh
# run.sh -- the REAL core 1 image (build/core1/q3.elf) in qemu-system-arm's
# Zynq-7000 (Cortex-A9, MMU, GIC, private timer), its 1 GB of DDR a shared
# file, and fake060.py as the 68k: what the board does, on the PC.
#
#   files/qemu/run.sh [-gdb] fake060-options...      (see fake060.py)
#
# ELF=<image.elf> runs another image of this harness (games/iortcw build/core1/wolf.elf).
# -gdb: a gdb stub on :12345 (arm-none-eabi-gdb build/core1/q3.elf, target remote :12345;
# attaching stops the core where it is)
# The ioquake3 image has no qemu-arm build: this is its only PC test.
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
QEMU=${QEMU_SYSTEM_ARM:-$HOME/opt/qemu/usr/bin/qemu-system-arm}
ddr=/dev/shm/zynq-q3.$$
gdb=
[ "$1" = -gdb ] && { gdb="-gdb tcp::12345"; shift; }
rm -f "$ddr"
# shellcheck disable=SC2086
"$QEMU" -M xilinx-zynq-a9 -m 1G -nographic -monitor none -serial null $gdb \
    -object memory-backend-file,id=ddr,size=1G,mem-path="$ddr",share=on -machine memory-backend=ddr \
    -device loader,file="${ELF:-$top/build/core1/q3.elf}",cpu-num=0 &
q=$!
trap 'kill $q 2>/dev/null; rm -f "$ddr"' EXIT INT TERM
while [ ! -s "$ddr" ]; do sleep 0.2; done
python3 -I "$here/fake060.py" "$ddr" "$@"
