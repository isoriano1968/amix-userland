#!/bin/sh
# build-cde.sh -- CDE 2.2.4a for AMIX, cross-built with gcc14-cross-amix.
#
# CDE builds with imake.  The patches add an AMIX platform (config/cf/amix.cf,
# host.def, config/amix/include for what SVR4.0 lacks) and the source changes
# the port needed.  Three things are prepared here before the build:
#   imports/            the X11R6.3 and Motif headers and static libraries
#   amix-cxxfix/        C++-usable copies of AMIX's K&R system headers,
#                       generated from your sysroot (amix-cxxfix.py)
#   config/cf/amixpaths.def  this build's paths
# CDE also runs tools of its own during the build (the help compiler,
# lineToData, makestrs, ...): hostbuild.sh builds those for the host.
# Everything is linked statically against X and Motif; AMIX's libc,
# libsocket and libnsl stay shared.
set -e
. "$(dirname "$0")/common.sh"
need gcc g++ make patch python3 bash cpp bdftopcf mkfontdir
[ -f "$MOTIF_ROOT/usr/motif/lib/libXm.a" ] || { echo "build Motif first (build-motif.sh)"; exit 1; }
[ -f "$LIBXP/libXp.a" ] || { echo "build libXp first (build-libxp.sh)"; exit 1; }
FIXED=$(ls -d "$TOOLCHAIN"/lib/gcc/m68k-elf/*/include-fixed | head -1)
[ -d "$FIXED" ] || { echo "no include-fixed under $TOOLCHAIN/lib/gcc/m68k-elf"; exit 1; }
export CDE

echo "== unpack and patch CDE"
rm -rf "$CDE"
tar -C "$WORK" -xzf "$DL/cde-src-$CDE_VER.tar.gz"
for p in "$TOP"/patches/cde/*.patch; do patch -s -d "$CDE" -p1 < "$p"; done
# CDE_SHARED=STAGE, from build-shared-cde.sh: position-independent, X and Motif
# from STAGE, and programs that find the libraries at their AMIX paths.  ld reads
# a library that only another .so names after the archives, so what those need
# from libgen, libm and the C++ runtime is asked for again at the end of the line.
if [ -n "$CDE_SHARED" ]; then
	sed -i -e 's|bin/m68k-elf-gcc$|bin/m68k-elf-gcc -fPIC|' \
		-e 's|bin/m68k-elf-g++ -fpermissive$|bin/m68k-elf-g++ -fpermissive -fPIC|' "$CDE/config/cf/host.def"
	cat >> "$CDE/config/cf/host.def" <<D
#define ExtraLoadFlags -Wl,-R,/usr/dt/lib:/usr/motif/lib:/usr/x11r6/lib -Wl,-rpath-link,$CDE_SHARED/usr/x11r6/lib -Wl,-rpath-link,$CDE_SHARED/usr/motif/lib -Wl,-rpath-link,$CDE_SHARED/usr/dt/lib -Wl,--as-needed -lXm -ltt -Wl,--no-as-needed -lgen -lm -lstdc++ -lsupc++ -lamixcde
D
fi
cat > "$CDE/config/cf/amixpaths.def" <<E
/* written by build-cde.sh */
#define AmixCrossPrefix		$TOOLCHAIN
#define AmixSysroot		$SYSROOT
#define AmixBuildTop		$CDE
E

echo "== imports, amix-cxxfix"
cd "$CDE/imports"
mkdir -p x11/lib x11/bin motif/lib
ln -s "$X11_EXPORTS/include" x11/include
for a in "$X11_EXPORTS"/lib/*.a; do ln -s "$a" x11/lib/; done
ln -sf "$LIBXP/libXp.a" x11/lib/libXp.a
ln -s "$(command -v bdftopcf)" "$(command -v mkfontdir)" x11/bin/
ln -s "$MOTIF_ROOT/usr/motif/include" motif/include
ln -s "$MOTIF_ROOT/usr/motif/lib/libXm.a" "$MOTIF_ROOT/usr/motif/lib/libMrm.a" motif/lib/
ln -s "$MOTIF_SRC/lib/Xm" motif/lib/Xm
if [ -n "$CDE_SHARED" ]; then
	ln -s "$CDE_SHARED"/usr/x11r6/lib/*.so "$CDE_SHARED"/usr/x11r6/lib/*.so.7 x11/lib/
	ln -s "$CDE_SHARED"/usr/motif/lib/*.so "$CDE_SHARED"/usr/motif/lib/*.so.4 motif/lib/
fi
python3 -I "$S/amix-cxxfix.py" "$SYSROOT" "$FIXED" "$CDE/amix-cxxfix"

echo "== imake and the Makefiles"
cd "$CDE"
export IMAKECPP="$S/imakecpp.sh"
(cd config/imake && make -f Makefile.ini BOOTSTRAPCFLAGS="-DAMIX -DSVR4 -Dm68k" \
	CC="gcc -std=gnu89 -fcommon -w" > "$CDE/imake-bootstrap.log" 2>&1)
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
[ -z "$CDE_SHARED" ] || sh "$S/shared-cde-libs.sh" "$CDE" "$CDE_SHARED"
echo "== programs (make-programs.log)"
(cd programs && make -k < /dev/null > ../make-programs.log 2>&1) || true
if [ -n "$CDE_SHARED" ]; then
	# tools that get Motif and ToolTalk only through other libraries: named
	# here, before the archives their needs come from
	F=$(sed -n 's/^#define ExtraLoadFlags //p' config/cf/host.def | sed 's/-Wl,--as-needed.*//')
	O=$(sed -n 's/^ *EXTRA_LIBRARIES = //p' programs/dtsr/Makefile | head -1)
	for d in programs/dtcm/dtcm programs/dtlogin programs/dtsr; do
		make -C "$d" -k EXTRA_LOAD_FLAGS="$F" < /dev/null \
			EXTRA_LDOPTIONS="-Wl,-u,_XtInherit -L$CDE_SHARED/usr/x11r6/lib -lXtInherit" EXTRA_LIBRARIES="-lDtHelp -lDtSvc -lXm -ltt $O -lgen -lm -lstdc++ -lsupc++ -lamixcde -lamixc99" \
			>> make-programs.log 2>&1 || true
	done
	# with widget classes of their own: one _XtInherit, in the program
	for p in dtwm/dtwm dtfile/dtfile dtprintinfo/dtprintinfo; do
		rm -f "programs/$p"
		d=programs/${p%/*}
		make -C "$d" -k EXTRA_LDOPTIONS="-Wl,-u,_XtInherit -L$CDE_SHARED/usr/x11r6/lib -lXtInherit" < /dev/null >> make-programs.log 2>&1 || true
	done
fi
echo "== fonts (make-fonts.log)"
make -C programs/fontaliases/linux/C FONTC=bdftopcf MKFONTDIR=mkfontdir < /dev/null > make-fonts.log 2>&1 || true
echo "== help volumes (make-help.log)"
(cd doc/C/help && ../../../config/imake/imake -I../../../config/cf -DTOPDIR=../../.. -DCURDIR=doc/C/help \
	&& make Makefiles && mkdir -p ../help-sdl && make -k < /dev/null) > make-help.log 2>&1 || true

echo "== result"
missing=0
for f in lib/DtSvc/libDtSvc.a lib/tt/lib/libtt.a lib/DtWidget/libDtWidget.a lib/DtHelp/libDtHelp.a \
	lib/DtTerm/libDtTerm.a lib/DtPrint/libDtPrint.a lib/DtMrm/libDtMrm.a lib/csa/libcsa.a \
	programs/dtwm/dtwm programs/dtsession/dtsession programs/dtlogin/dtlogin programs/dtterm/dtterm \
	programs/dtfile/dtfile programs/dtpad/dtpad programs/dtstyle/dtstyle programs/dtexec/dtexec \
	programs/dtspcd/dtspcd programs/dthelp/dthelpview/dthelpview lib/tt/bin/ttsession/ttsession \
	programs/fontaliases/linux/C/dtinfo.pcf; do
	if [ -f "$f" ]; then echo "  ok      $f"; else echo "  MISSING $f"; missing=1; fi
done
[ $missing = 0 ] || { echo "some parts did not build: see the logs in $CDE"; exit 1; }
