#!/bin/sh
# build.sh [app ...] -- the apps against the shared Motif and X11R6.3, by gcc14-cross-amix.
# Flags come from pkg-config (tools/amix-pkg-config, the AMIX sysroot), with
# runpaths to /usr/motif/lib, /usr/x11r6/lib and /usr/local/lib on the box.
set -e
cd "$(dirname "$0")"
P=../../../tools/amix-pkg-config
for app in ${*:-amixcast ztopcde amixsysmon}; do
	pkgs=xm
	[ $app = amixsysmon ] && pkgs="xm amixstat"	# library/amixstat
	m68k-elf-gcc -O2 -m68060 -fno-strict-aliasing -Wno-implicit-int -Wno-implicit-function-declaration \
	  -Wno-incompatible-pointer-types -Wno-int-conversion -DFUNCPROTO=15 \
	  $($P --cflags $pkgs) -o $app/$app $app/$app.c $($P --libs $pkgs)
	../../../tools/amix-fix-needed $app/$app > /dev/null	# AMIX's ld.so.1 reads NEEDED names early
	ls -l $app/$app
done
