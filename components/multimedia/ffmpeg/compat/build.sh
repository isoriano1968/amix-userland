#!/bin/sh
# build.sh -- libvplaycompat.a: what AMIX lacks for FFmpeg, vplay and ffplay
# (m68k-elf-gcc of gcc14-cross-amix on PATH).
#   amix-ll.c     strtoll, strtoull, llabs
#   amix-math.c   rounding and square roots with the 68060's FPU, scalbn
#   amix-mem.c    memcpy, memmove, memset, memcmp
#   amix-clock.c  gettimeofday from the kernel's clock, no system call
#   amix-net.c    connect for FFmpeg (see there)
set -e
C=$(cd "$(dirname "$0")" && pwd)
cd "$C"
rm -f libvplaycompat.a
for f in amix-ll amix-math amix-mem amix-clock amix-net; do
	m68k-elf-gcc -O2 -m68060 -fno-strict-aliasing -Iinclude -c $f.c -o $f.o
done
m68k-elf-ar rc libvplaycompat.a amix-ll.o amix-math.o amix-mem.o amix-clock.o amix-net.o
m68k-elf-ranlib libvplaycompat.a
ls -l libvplaycompat.a
