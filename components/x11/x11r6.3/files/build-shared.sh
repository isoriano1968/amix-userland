#!/bin/sh
# build-shared.sh XC STAGE -- the X11R6.3 client libraries as ELF shared
# libraries from gcc14-cross-amix.  XC: a copy of the tree build060.sh uses,
# built in place.  STAGE: where usr/x11r6/lib and its pkgconfig go.
#
# These are not AMIX's split code/data images: no .sa stubs, each library
# names what it needs, and the sonames are .so.7 so that they can sit beside
# the gcc 2.7 libraries in /usr/x11r6/lib/r63 until those go.  Programs that
# use them take what is left to the program from libc's stub (select, __iob).
#
# The toolchain, CPU and sysroot come from the environment (the component
# Makefile sets them; the defaults are the P52's):
#   AMIX_TOOLS    gcc-14's bin directory     ($HOME/opt/m68k-amix/bin)
#   AMIX_CPU      the CPU option  (-mcpu=68020 -mtune=68060 -fexcess-precision=fast)
#   AMIX_SYSROOT  passed as --sysroot, so gcc-14 sees only that sysroot and
#                 not the one it was configured with (unset: its own)
set -e
XC=$1; STAGE=$2
HERE=$(cd "$(dirname "$0")" && pwd)	# files/, before any cd
TOOLS=$(cd "$HERE/../../../.." && pwd)/tools
export PATH=${AMIX_TOOLS:-$HOME/opt/m68k-amix/bin}:$PATH
CPU=${AMIX_CPU:--mcpu=68020 -mtune=68060 -fexcess-precision=fast}
CC14="m68k-elf-gcc${AMIX_SYSROOT:+ --sysroot=$AMIX_SYSROOT}"
L=$STAGE/usr/x11r6/lib
mkdir -p "$L/pkgconfig" "$XC/log"
PR=$XC/log/print.mk
printf 'print-objs:\n\t@echo $(OBJS)\n' > "$PR"
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
	make -k CC="$CC14" CDEBUGFLAGS="-O2 $CPU -fPIC" \
		STD_DEFINES="-DAMIX -Dm68k -DSVR4 -D__amix__ -U__linux__ -Ulinux -ansi -w" \
		SHLIBDEF= SHAREDCODEDEF= PICFLAGS= $objs > "$XC/log/$l.log" 2>&1 || true
	for o in $objs; do [ -f "$o" ] || { echo "$l: no $o"; exit 1; }; done
	"$TOOLS/amix-got-check" $objs
	if [ "$l" = Xdmcp ]; then
		$CC14 -O2 $CPU -fPIC -c "$HERE/rand48.c" -o rand48.o
		objs="$objs rand48.o"
	fi
	deps=
	for d in "$@"; do deps="$deps -l$d"; done
	$CC14 -shared -Wl,-h,lib$l.so.7 $objs -L"$L" $deps -o "$L/lib$l.so.7"
	"$TOOLS/amix-fix-data-relocs" "$L/lib$l.so.7"
	"$TOOLS/amix-fix-needed" "$L/lib$l.so.7"
	m68k-elf-strip "$L/lib$l.so.7"
	ln -sf "lib$l.so.7" "$L/lib$l.so"
	echo "$l: $(echo $objs | wc -w) objects, $(wc -c < "$L/lib$l.so.7") bytes"
	pc=$(echo "$l" | tr 'A-Z' 'a-z')
	# what a program linking -l$l needs as well: the other X libraries its
	# headers use, X11's socket libraries on SVR4 (Xlib talks TLI/sockets)
	# and libamixc99, Xt's libXtInherit.a (xtinherit.c says why)
	pre= post= req=
	case $l in
	X11)	req=amixc99; post=" -lsocket -lnsl" ;;
	ICE|Xext) req=x11 ;;
	SM)	req="ice x11" ;;
	Xi)	req="xext x11" ;;
	Xt)	req="sm ice x11"; pre=" -lXtInherit" ;;
	Xmu)	req="xt xext x11" ;;
	Xaw)	req="xmu xt xext x11" ;;
	esac
	cat > "$L/pkgconfig/$pc.pc" <<P
prefix=/usr/x11r6
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $l
Description: X11R6.3 lib$l
Version: 6.3
${req:+Requires: $req
}Libs: -L\${libdir}$pre -l$l$post
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
$CC14 -O2 $CPU -I"$XC/exports/include" -D__amix__ -DSVR4 -w -c "$HERE/xtinherit.c" -o "$L/xtinherit.o"
rm -f "$L/libXtInherit.a"
m68k-elf-ar rcs "$L/libXtInherit.a" "$L/xtinherit.o"
rm -f "$L/xtinherit.o"

# xauth, from the unchanged R6.3 sources against these libraries: AMIX's own
# (X11R5, /usr/X/bin) turns the hex letters of a cookie into digits, which
# breaks ssh -X (network/openssh's sshrc and XAuthLocation use this one)
mkdir -p "$STAGE/usr/x11r6/bin"
$CC14 -O2 $CPU -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
	-Wno-error=incompatible-pointer-types -Wno-error=implicit-int -D__amix__ -DSVR4 \
	-DTCPCONN -DUNIXCONN -DFUNCPROTO=15 -DNARROWPROTO -I"$XC" -I"$XC/exports/include" \
	"$XC"/programs/xauth/xauth.c "$XC"/programs/xauth/gethost.c "$XC"/programs/xauth/process.c \
	"$XC"/programs/xauth/parsedpy.c -L"$L" -Wl,-rpath-link,"$L" -Wl,-R,/usr/x11r6/lib \
	-lXmu -lXt -lSM -lICE -lXext -lX11 -lXau -lsocket -lnsl -o "$STAGE/usr/x11r6/bin/xauth"
m68k-elf-strip "$STAGE/usr/x11r6/bin/xauth"

# the X programs CDE's session runs (xinit for startcde, xset and xsetroot in
# Xsession, xrdb, xterm for the failsafe session, xmodmap for the Amiga keymaps), from the unchanged R6.3
# sources: until 2026-11-20 they were stock AMIX's X11R5 ones (/usr/X/bin,
# Commodore's Xbasic), which an AMIX without X lacks.  K&R C: -std=gnu89.
CL="-std=gnu89 -O2 $CPU -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
	-Wno-error=incompatible-pointer-types -Wno-error=implicit-int -D__amix__ -DSVR4 \
	-DTCPCONN -DUNIXCONN -DFUNCPROTO=15 -DNARROWPROTO -I$XC -I$XC/exports/include"
LK="-L$L -Wl,-rpath-link,$L -Wl,-R,/usr/x11r6/lib"
XLIBS="-lXmu -lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl"
B="$STAGE/usr/x11r6/bin"
$CC14 $CL -DXINITDIR=\"/usr/x11r6/lib/X11/xinit\" -DBINDIR=\"/usr/x11r6/bin\" \
	"$XC"/programs/xinit/xinit.c $LK -lX11 -lsocket -lnsl -o "$B/xinit"
for p in xset xsetroot; do
	$CC14 $CL "$XC/programs/$p/$p.c" $LK $XLIBS -o "$B/$p"
done
# xmodmap: the Amiga keyboard layouts (xkeymap-amiga)
( cd "$XC/programs/xmodmap" && $CC14 $CL xmodmap.c pf.c handle.c exec.c $LK $XLIBS -o "$B/xmodmap" )
# xrdb runs the C preprocessor of AMIX's own compilers (stock: /usr/ccs/lib/cpp);
# without one, xrdb -nocpp
$CC14 $CL -DCPP=\"/usr/ccs/lib/cpp\" "$XC"/programs/xrdb/xrdb.c $LK $XLIBS -o "$B/xrdb"
# xterm: termcap from library/ncurses (/usr/local/lib), utmp entries as R6.3's
# Imakefile (-DUTMP); not setuid
( cd "$XC/programs/xterm" && $CC14 $CL -DUTMP button.c charproc.c cursor.c data.c input.c main.c \
	menu.c misc.c screen.c scrollbar.c tabs.c Tekproc.c TekPrsTbl.c util.c VTPrsTbl.c \
	$LK -Wl,-R,/usr/local/lib -L"${AMIX_SYSROOT:-/nonexistent}/usr/local/lib" -lXaw $XLIBS -lncurses \
	-o "$B/xterm" )
mkdir -p "$STAGE/usr/x11r6/lib/X11/app-defaults"
cp "$XC/programs/xterm/XTerm.ad" "$STAGE/usr/x11r6/lib/X11/app-defaults/XTerm"
for p in xinit xset xsetroot xrdb xterm xmodmap; do m68k-elf-strip "$B/$p"; done

# Xlib's data in /usr/x11r6/lib/X11 (its compiled-in paths): the locale
# database (XLOCALEDIR, xc/nls: without it XSupportsLocale fails, Xlib
# says "locale not supported", and CDE's font sets come out empty: dtwm
# stopped with "failed to load font") and Xcms.txt.  The nls files through cpp as R6.3's CppFileTarget
# runs it (WCHAR32: AMIX's wchar_t is 32 bits); the C locale is AMIX's own,
# with both halves of ISO 8859-1 (files/XLC_LOCALE.C.amix, umlauts in dtterm).
X11D="$STAGE/usr/x11r6/lib/X11"
LD="$X11D/locale"
nlscpp() {	# nlscpp SRC DEST
	cpp -traditional -P -undef -DWCHAR32=1 < "$1" | \
		sed -e '/^#  *[0-9][0-9]*  *.*$/d' -e '/^XCOMM$/s//#/' -e '/^XCOMM[^a-zA-Z0-9_]/s/^XCOMM/#/' > "$2"
}
rm -rf "$LD"; mkdir -p "$LD/tbl_data"
for f in locale.alias locale.dir compose.dir; do nlscpp "$XC/nls/$f" "$LD/$f"; done
for n in C en_US.utf iso8859-1 iso8859-2 iso8859-3 iso8859-4 iso8859-5 iso8859-6 iso8859-7 iso8859-8 \
	 iso8859-9 ja ja.JIS ja.S90 ja.SJIS ja.U90 ko th_TH.TACTIS zh zh_TW; do
	mkdir -p "$LD/$n"
	nlscpp "$XC/nls/XLC_LOCALE/$n" "$LD/$n/XLC_LOCALE"
done
cp "$HERE/XLC_LOCALE.C.amix" "$LD/C/XLC_LOCALE"
for n in iso8859-1 iso8859-2; do cp "$XC/nls/Compose/$n" "$LD/$n/Compose"; done
for f in tab8859_1 tab8859_2 tab8859_3 tab8859_4 tab8859_5 tab8859_6 tab8859_7 tab8859_8 tab8859_9 \
	 jis0208 ksc5601 gb2312 jis0201; do cp "$XC/nls/XLC_LOCALE/$f" "$LD/tbl_data/"; done
# (XErrorDB and XKeysymDB: desktop/cde has shipped them since before this
# package did; a file moving between packages needs amix-pkg 0.5.1 on the box)
cp "$XC/lib/X11/Xcms.txt" "$X11D/"
