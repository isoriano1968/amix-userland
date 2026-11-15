#!/bin/sh
# build-cde.sh -- CDE 2.2.4a for AMIX on shared libraries, cross-built with
# gcc 14 in prep.sh's tree.
#
# CDE builds with imake.  The patches add an AMIX platform (config/cf/amix.cf,
# host.def, config/amix/include for what SVR4.0 lacks) and the source changes
# the port needed; prep.sh put this build's paths into host.def.  CDE also
# runs tools of its own during the build (the help compiler, lineToData,
# makestrs, ...): hostbuild.sh builds those for the host.
#
# Everything is compiled -fPIC.  After the libraries, shared-cde-libs.sh makes
# libDt*.so.2, libtt.so.2 and libcsa.so.2 from their archives in
# build/shared/usr/dt/lib; the programs then link those, and X, libXp and
# Motif from the sysroot.  AMIX's libc, libsocket and libnsl are shared.
set -e
. "$(dirname "$0")/common.sh"
need "$HOST_CC" "$HOST_CXX" make python3 bash cpp flex bison bdftopcf mkfontdir
# CDE's Makefiles build serially, on their own: nothing of the outer make's
# flags (jobs, variables) reaches them
unset MAKEFLAGS MFLAGS MAKELEVEL MAKEOVERRIDES

echo "== libXtInherit.a"
# a program's own _XtInherit (xtinherit.c says why), compiled as x11/x11r6.3's
# build-shared.sh compiles it; it is not in the x11r6.3 package (the Amiga's
# own gcc builds it there), so the programs get it from here
rm -rf "$XTI"; mkdir -p "$XTI"
$CC14 -O2 $CPU -I"$X11/include" -D__amix__ -DSVR4 -w -c "$TOP/../../x11/x11r6.3/files/xtinherit.c" -o "$XTI/xtinherit.o"
m68k-elf-ar rcs "$XTI/libXtInherit.a" "$XTI/xtinherit.o"
rm -f "$XTI/xtinherit.o"

echo "== imake and the Makefiles"
cd "$CDE"
export IMAKECPP="$S/imakecpp.sh"
(cd config/imake && make -f Makefile.ini BOOTSTRAPCFLAGS="-DAMIX -DSVR4 -Dm68k" \
	CC="$HOST_CC -std=gnu89 -fcommon -w" > "$CDE/imake-bootstrap.log" 2>&1)
./config/imake/imake -I./config/cf -s xmakefile -DTOPDIR=. -DCURDIR=. > xmakefile.log 2>&1
make -f xmakefile Makefiles > make-Makefiles.log 2>&1

echo "== host tools"
H=$S/hostbuild.sh
CLEAN="*.o makestrs lndir" sh "$H" config/util all > hb-config-util.log 2>&1
CLEAN="*.o makedepend" sh "$H" config/makedepend all > hb-makedepend.log 2>&1
sh "$H" lib/DtTerm/Term TermLineData.c > hb-lineToData.log 2>&1
sh "$H" programs/dthelp/parser includes > hb-dthelp-inc.log 2>&1
CLEAN="*.o" sh "$H" programs/dtinfo/tools/misc all > hb-dtinfo-misc.log 2>&1
CLEAN="*.o" sh "$H" programs/localized/util merge mkcatdefs > hb-localized.log 2>&1

# make -k throughout: a few parts do not build for AMIX (see README), and
# gencat waits on stdin unless it is redirected
echo "== includes"
make -k -f xmakefile includes < /dev/null > make-includes.log 2>&1 || true
# the help compiler's passes need headers that "includes" puts in place
sh "$H" programs/dthelp/parser all > hb-dthelp-parser.log 2>&1 || true
echo "== libraries (make-lib.log)"
(cd lib && make -k < /dev/null > ../make-lib.log 2>&1) || true
rm -rf "$OUT"
sh "$S/shared-cde-libs.sh" "$CDE" "$OUT"
echo "== programs (make-programs.log)"
(cd programs && make -k < /dev/null > ../make-programs.log 2>&1) || true
# tools that get Motif and ToolTalk only through other libraries: named
# here, before the archives their needs come from
F=$(sed -n 's/^#define ExtraLoadFlags //p' config/cf/host.def | sed 's/-Wl,--as-needed.*//')
O=$(sed -n 's/^ *EXTRA_LIBRARIES = //p' programs/dtsr/Makefile | head -1)
rm -f programs/dtcm/dtcm/dtcm
for d in programs/dtcm/dtcm programs/dtlogin programs/dtsr; do
	make -C "$d" -k EXTRA_LOAD_FLAGS="$F" < /dev/null \
		EXTRA_LDOPTIONS="-Wl,-u,_XtInherit -L$XTI -lXtInherit" EXTRA_LIBRARIES="-lDtHelp -lDtSvc -lXm -ltt $O -lgen -lm -lstdc++ -lsupc++ -lamixcde -lamixc99" \
		>> make-programs.log 2>&1 || true
done
# with widget classes of their own: one _XtInherit, in the program
for p in dtwm/dtwm dtfile/dtfile dtprintinfo/dtprintinfo; do
	rm -f "programs/$p"
	d=programs/${p%/*}
	make -C "$d" -k EXTRA_LDOPTIONS="-Wl,-u,_XtInherit -L$XTI -lXtInherit" < /dev/null >> make-programs.log 2>&1 || true
done
echo "== fonts (make-fonts.log)"
make -C programs/fontaliases/linux/C FONTC=bdftopcf MKFONTDIR=mkfontdir < /dev/null > make-fonts.log 2>&1 || true
echo "== help volumes (make-help.log)"
(cd doc/C/help && ../../../config/imake/imake -I../../../config/cf -DTOPDIR=../../.. -DCURDIR=doc/C/help \
	&& make Makefiles && mkdir -p ../help-sdl && make -k < /dev/null) > make-help.log 2>&1 || true

echo "== result"
missing=0
for f in lib/DtSvc/libDtSvc.so.2 lib/tt/lib/libtt.so.2 lib/DtWidget/libDtWidget.so.2 lib/DtHelp/libDtHelp.so.2 \
	lib/DtTerm/libDtTerm.so.2 lib/DtPrint/libDtPrint.so.2 lib/DtMrm/libDtMrm.so.2 lib/DtSearch/libDtSearch.so.2 \
	lib/csa/libcsa.so.2 \
	programs/dtwm/dtwm programs/dtsession/dtsession programs/dtlogin/dtlogin programs/dtterm/dtterm \
	programs/dtfile/dtfile programs/dtpad/dtpad programs/dtstyle/dtstyle programs/dtexec/dtexec \
	programs/dtspcd/dtspcd programs/dthelp/dthelpview/dthelpview lib/tt/bin/ttsession/ttsession \
	programs/dtcalc/dtcalc programs/dtcm/dtcm/dtcm programs/dtprintinfo/dtprintinfo \
	programs/fontaliases/linux/C/dtinfo.pcf; do
	if [ -f "$f" ]; then echo "  ok      $f"; else echo "  MISSING $f"; missing=1; fi
done
[ $missing = 0 ] || { echo "some parts did not build: see the logs in $CDE"; exit 1; }
