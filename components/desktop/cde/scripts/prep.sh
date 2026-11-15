#!/bin/sh
# prep.sh -- the CDE tree for the shared build, from the pinned inputs:
#   cde-2.2.4a/          CDE unpacked and patched (patches/cde)
#   motif-2.3.8/         Motif unpacked and patched (desktop/motif's patches):
#                        CDE's build reads lib/Xm through imports/motif/lib/Xm
#   imports/             links to the X11R6.3, libXp and Motif headers and
#                        shared libraries in the sysroot
#   amix-cxxfix/         C++-usable copies of AMIX's K&R system headers,
#                        generated from the sysroot (amix-cxxfix.py)
#   config/cf/amixpaths.def, host.def   this build's paths: gcc 14 with
#                        --sysroot and -fPIC, the programs' runpath, the
#                        libraries at link time
set -e
. "$(dirname "$0")/common.sh"
need patch python3 bdftopcf mkfontdir
FIXED=$(m68k-elf-gcc -print-file-name=include-fixed)
[ -d "$FIXED" ] || { echo "no include-fixed in gcc 14 ($FIXED)"; exit 1; }
mkdir -p "$WORK"

echo "== unpack and patch CDE"
rm -rf "$CDE"
tar -C "$WORK" -xzf "${CDE_ARCHIVE:?}"
for p in "$TOP"/patches/cde/*.patch; do patch -s -d "$CDE" -p1 < "$p"; done

echo "== unpack and patch Motif (lib/Xm)"
rm -rf "$MOTIF_SRC"
tar -C "$WORK" -xzf "${MOTIF_ARCHIVE:?}"
for p in "${MOTIF_DIR:?}"/patches/*.patch; do patch -s -d "$MOTIF_SRC" -p1 < "$p"; done

echo "== host.def, amixpaths.def"
# position-independent, gcc 14 with this sysroot only; programs find the
# libraries at their AMIX paths and the build's libraries at link time.  ld
# reads a library that only another .so names after the archives, so what
# those need from libgen, libm and the C++ runtime is asked for again at the
# end of the line.
H=$CDE/config/cf/host.def
sed -i -e "s|bin/m68k-elf-gcc\$|bin/m68k-elf-gcc --sysroot=$SYSROOT $CPU -fPIC|" \
	-e "s|bin/m68k-elf-g++ -fpermissive\$|bin/m68k-elf-g++ --sysroot=$SYSROOT $CPU -fpermissive -fPIC|" "$H"
[ "$(grep -c -- "--sysroot=$SYSROOT $CPU .*-fPIC\$" "$H")" = 2 ] || { echo "$H: CcCmd/CplusplusCmd not found"; exit 1; }
# -lXp is there for the static libXm's printing; the shared libXm has no Xp
# and nothing else here calls it, so linking libXp.so only adds a NEEDED
sed -i -e '/^#define XmExtraLibraries/s/\([[:space:]]\)-lXp /\1/' -e '/^#define ExtraLibraries/s/\([[:space:]]\)-lXp /\1/' "$H"
! grep -q -- '-lXp[[:space:]]' "$H" || { echo "$H: -lXp still there"; exit 1; }
# imports/ holds X11's and Motif's shared libraries only (the sysroot has no
# archives of them), so they are declared shared: their DEP*LIB variables
# are then empty instead of naming imports/*/lib/lib*.a, which no rule makes
cat >> "$H" <<D
#define SharedLibX11		YES
#define SharedLibXext		YES
#define SharedLibXau		YES
#define SharedLibXdmcp		YES
#define SharedLibXmu		YES
#define SharedLibXt		YES
#define SharedLibXaw		YES
#define SharedLibXi		YES
#define SharedLibICE		YES
#define SharedLibSM		YES
#define SharedLibXm		YES
#define SharedLibMrm		YES
#define ExtraLoadFlags -Wl,-R,/usr/dt/lib:/usr/motif/lib:/usr/x11r6/lib -Wl,-rpath-link,$X11/lib -Wl,-rpath-link,$MOTIF/lib -Wl,-rpath-link,$OUT/usr/dt/lib -Wl,--as-needed -lXm -ltt -Wl,--no-as-needed -lgen -lm -lstdc++ -lsupc++ -lamixcde
D
cat > "$CDE/config/cf/amixpaths.def" <<E
/* written by prep.sh */
#define AmixCrossPrefix		$(cd "$AMIX_TOOLS/.." && pwd)
#define AmixSysroot		$SYSROOT
#define AmixBuildTop		$CDE
E

echo "== imports"
cd "$CDE/imports"
mkdir -p x11/lib x11/bin motif/lib
ln -s "$X11/include" x11/include
ln -s "$X11"/lib/*.so "$X11"/lib/*.so.7 x11/lib/
ln -s "$(command -v bdftopcf)" "$(command -v mkfontdir)" x11/bin/
ln -s "$MOTIF/include" motif/include
ln -s "$MOTIF"/lib/*.so "$MOTIF"/lib/*.so.4 motif/lib/
ln -s "$MOTIF_SRC/lib/Xm" motif/lib/Xm

echo "== amix-cxxfix"
python3 -I "$S/amix-cxxfix.py" "$SYSROOT" "$FIXED" "$CDE/amix-cxxfix"
