#!/bin/sh
# build.sh [app ...] -- the apps against the shared Motif and X11R6.3, by gcc14-cross-amix.
# Flags come from pkg-config (tools/amix-pkg-config, the AMIX sysroot), with
# runpaths to /usr/motif/lib, /usr/x11r6/lib and /usr/local/lib on the box.
set -e
cd "$(dirname "$0")"
P=../../../tools/amix-pkg-config
# the AMIX baseline: 68020/030 with a 68881/2 up to the 68060 (doc/CPU-TARGET-68020-60.md)
CPU=${AMIX_CPU:--mcpu=68020 -mtune=68060 -fexcess-precision=fast}
export AMIX_CPU="$CPU"
# a program's own _XtInherit (x11r6.3's files/xtinherit.c says why), as
# desktop/cde builds it: xm.pc names -lXtInherit, which the sysroot's X11R6.3
# does not ship (on AMIX the Amiga's own gcc makes it)
XTI=build/xtinherit
mkdir -p $XTI
m68k-elf-gcc -O2 $CPU -I"${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}/usr/x11r6/include" -D__amix__ -DSVR4 -w \
	-c ../../x11/x11r6.3/files/xtinherit.c -o $XTI/xtinherit.o
m68k-elf-ar rcs $XTI/libXtInherit.a $XTI/xtinherit.o
rm -f $XTI/xtinherit.o
for app in ${*:-amixcast ztopcde amixsysmon amixdisplay amixuae amixprinters amixq3 amixsoftware}; do
	if [ $app = amixcast ]; then	# libupnp, Pth and core 1's zzav.h: its own script
		XTI=$XTI sh amixcast/build-amixcast.sh
		../../../tools/amix-fix-needed amixcast/amixcast > /dev/null
		continue
	fi
	pkgs=xm
	[ $app = amixsysmon ] && pkgs="xm amixstat"	# library/amixstat
	[ $app = amixdisplay ] && pkgs="xm xrandr"	# x11/libxrandr
	net=
	[ $app = amixsoftware ] && net="-lsocket -lnsl"	# Find on Network (UDP)
	m68k-elf-gcc -O2 $CPU -fno-strict-aliasing -Wno-implicit-int -Wno-implicit-function-declaration \
	  -Wno-incompatible-pointer-types -Wno-int-conversion -DFUNCPROTO=15 \
	  $($P --cflags $pkgs) -o $app/$app $app/$app.c -L$XTI $($P --libs $pkgs) $net
	../../../tools/amix-fix-needed $app/$app > /dev/null	# AMIX's ld.so.1 reads NEEDED names early
	ls -l $app/$app
	if [ $app = amixdisplay ]; then	# the setuid helper: libc only, no X
		m68k-elf-gcc -O2 $CPU -Wall $($P --cflags-only-I xm 2>/dev/null | sed 's/-I[^ ]*//g') \
		  -o amixdisplay/setlogin amixdisplay/setlogin.c
		../../../tools/amix-fix-needed amixdisplay/setlogin > /dev/null
		ls -l amixdisplay/setlogin
	fi
	if [ $app = amixsoftware ]; then	# the setuid root helper: libc only, no X
		m68k-elf-gcc -O2 $CPU -Wall -o amixsoftware/swrun amixsoftware/swrun.c
		../../../tools/amix-fix-needed amixsoftware/swrun > /dev/null
		ls -l amixsoftware/swrun
	fi
	if [ $app = amixprinters ]; then	# the search tool, the sender, the setuid lp helper: no X
		for t in amixprfind prsend prset; do
			m68k-elf-gcc -O2 $CPU -Wall -Wno-implicit-function-declaration -Wno-pointer-sign \
			  -o amixprinters/$t amixprinters/$t.c -lsocket -lnsl
			../../../tools/amix-fix-needed amixprinters/$t > /dev/null
			ls -l amixprinters/$t
		done
	fi
done
