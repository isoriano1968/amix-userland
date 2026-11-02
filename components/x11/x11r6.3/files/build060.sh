#!/bin/sh
# build060.sh -- the X11R6.3 client libraries as static archives built by
# gcc14-cross-amix for the 68060 (-O2 -m68060), in lib060/.  AMIX's own are
# gcc 2.7.2.3 -O for the 68020; CDE and Motif link them statically.
# Source: xc060/, a copy of x11/xzz9000/src/xc after its build.sh step 4
# (imake Makefiles for the AMIX cross configuration).  Each library's own
# Makefile compiles (so per-file defines like ErrDes.c's ERRORDB stay);
# only the compiler and its flags are replaced.
#   sh build060.sh [lib ...]      default: X11 Xt Xext Xmu ICE SM Xau Xdmcp Xaw
set -e
H=$(cd "$(dirname "$0")" && pwd)
export PATH=$HOME/opt/m68k-amix/bin:$PATH
LIBS=${*:-X11 Xt Xext Xmu ICE SM Xau Xdmcp Xaw}
mkdir -p "$H/lib060" "$H/log060"
PR=$H/log060/print.mk
# X11's ks_tables.h comes from makekeys, a host tool
X=$H/xc060/lib/X11
if [ ! -f "$X/ks_tables.h" ]; then
	gcc -std=gnu89 -w -I$H/xc060 -I$H/xc060/exports/include -o "$X/util/makekeys-host" "$X/util/makekeys.c"
	"$X/util/makekeys-host" < "$H/xc060/exports/include/X11/keysymdef.h" > "$X/ks_tables.h"
fi
printf 'print-objs:\n\t@echo $(OBJS)\n' > "$PR"
for l in $LIBS; do
	cd "$H/xc060/lib/$l"
	objs=$(make -s -f Makefile -f "$PR" print-objs)
	# Xext's Makefile predates HasShm YES; AMIX's own libXext.a has XShm.o
	[ $l = Xext ] && case " $objs " in *" XShm.o "*) ;; *) objs="$objs XShm.o";; esac
	rm -f $objs
	make -k CC=m68k-elf-gcc CDEBUGFLAGS="-O2 -m68060" \
		STD_DEFINES="-DAMIX -Dm68k -DSVR4 -D__amix__ -U__linux__ -Ulinux -ansi -w" \
		SHLIBDEF= SHAREDCODEDEF= PICFLAGS= $objs > "$H/log060/$l.log" 2>&1 || true
	missing=0
	for o in $objs; do [ -f "$o" ] || { echo "$l: no $o"; missing=1; }; done
	rm -f "$H/lib060/lib$l.a"
	m68k-elf-ar rcs "$H/lib060/lib$l.a" $objs
	echo "$l: $(echo $objs | wc -w) objects -> lib060/lib$l.a $([ $missing = 0 ] && echo ok || echo INCOMPLETE)"
done
