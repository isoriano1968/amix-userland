#!/bin/sh
# build-shared.sh XC STAGE -- the X11R6.3 client libraries as ELF shared
# libraries from gcc14-cross-amix.  XC: a copy of the tree build060.sh uses,
# built in place.  STAGE: where usr/x11r6/lib and its pkgconfig go.
#
# These are not AMIX's split code/data images: no .sa stubs, each library
# names what it needs, and the sonames are .so.7 so that they can sit beside
# the gcc 2.7 libraries in /usr/x11r6/lib/r63 until those go.  Programs that
# use them take what is left to the program from libc's stub (select, __iob).
set -e
XC=$1; STAGE=$2
HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$(cd "$HERE/../../../.." && pwd)/tools
export PATH=$HOME/opt/m68k-amix/bin:$PATH
L=$STAGE/usr/x11r6/lib
mkdir -p "$L/pkgconfig" "$XC/log"
PR=$XC/log/print.mk
printf 'print-objs:\n\t@echo $(OBJS)\n' > "$PR"
grep -q '(defined(SVR4) && !defined(__amix__))' "$XC/include/Xos.h" ||
	sed -i 's/^#if defined(SVR4) || defined(VMS) || defined(WIN32)$/#if (defined(SVR4) \&\& !defined(__amix__)) || defined(VMS) || defined(WIN32)/' "$XC/include/Xos.h"
X=$XC/lib/X11
if [ ! -f "$X/ks_tables.h" ]; then
	gcc -std=gnu89 -w -I"$XC" -I"$XC/exports/include" -o "$X/util/makekeys-host" "$X/util/makekeys.c"
	"$X/util/makekeys-host" < "$XC/exports/include/X11/keysymdef.h" > "$X/ks_tables.h"
fi

# library, then the ones it needs
lib() {
	l=$1; shift
	case " ${ONLY:-$l} " in *" $l "*) ;; *) return 0;; esac
	cd "$XC/lib/$l"
	objs=$(make -s -f Makefile -f "$PR" print-objs)
	[ "$l" = Xext ] && case " $objs " in *" XShm.o "*) ;; *) objs="$objs XShm.o";; esac
	rm -f $objs
	make -k CC=m68k-elf-gcc CDEBUGFLAGS="-O2 -m68060 -fPIC" \
		STD_DEFINES="-DAMIX -Dm68k -DSVR4 -D__amix__ -U__linux__ -Ulinux -ansi -w" \
		SHLIBDEF= SHAREDCODEDEF= PICFLAGS= $objs > "$XC/log/$l.log" 2>&1 || true
	for o in $objs; do [ -f "$o" ] || { echo "$l: no $o"; exit 1; }; done
	"$TOOLS/amix-got-check" $objs
	if [ "$l" = Xdmcp ]; then
		m68k-elf-gcc -O2 -m68060 -fPIC -c "$HERE/rand48.c" -o rand48.o
		objs="$objs rand48.o"
	fi
	deps=
	for d in "$@"; do deps="$deps -l$d"; done
	m68k-elf-gcc -shared -Wl,-h,lib$l.so.7 $objs -L"$L" $deps -o "$L/lib$l.so.7"
	"$TOOLS/amix-fix-data-relocs" "$L/lib$l.so.7"
	ln -sf "lib$l.so.7" "$L/lib$l.so"
	echo "$l: $(echo $objs | wc -w) objects, $(wc -c < "$L/lib$l.so.7") bytes"
	pc=$(echo "$l" | tr 'A-Z' 'a-z')
	cat > "$L/pkgconfig/$pc.pc" <<P
prefix=/usr/x11r6
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $l
Description: X11R6.3 lib$l
Version: 6.3
Libs: -L\${libdir} -l$l
Cflags: -I\${includedir}
P
}

lib Xau
lib Xdmcp
lib X11 Xau Xdmcp
lib ICE
lib SM ICE
lib Xext X11
lib Xi Xext X11
lib Xt SM ICE X11
lib Xmu Xt Xext X11
lib Xaw Xmu Xt Xext X11

# a program's own _XtInherit: xtinherit.c says why
m68k-elf-gcc -O2 -m68060 -I"$XC/exports/include" -D__amix__ -DSVR4 -w -c "$HERE/xtinherit.c" -o "$L/xtinherit.o"
rm -f "$L/libXtInherit.a"
m68k-elf-ar rcs "$L/libXtInherit.a" "$L/xtinherit.o"
rm -f "$L/xtinherit.o"
