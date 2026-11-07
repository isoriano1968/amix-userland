#!/bin/sh
# build.sh -- cross-build the X11R6.3 Xzz9000 server for the A3000 + Z3660 RTG
# (amix-z3660rtg docs/X-CROSS.md, with the fork's x11/patches), from the local
# copy of isoriano's X11R6.3 tree.  Every step logs to log/.
#   sh build.sh            full build from a fresh copy of the tree
# Inputs:
#   XC_SRC   the X11R6.3 tree (default usr-amix/usr/x11r6/xc; copied, never edited)
#   RTG      amix-z3660rtg clone (default usr-amix/upstream/amix-z3660rtg)
#   DISTFILES  xorg-server-1.0.2 and randrproto-1.1.2 tarballs (default ./distfiles,
#            checked against SOURCES.sha256)
#   HDF      AMIX disk image for /usr/amiga and /usr/ucblib (read-only)
# Output: src/xc/programs/Xserver/Xzz9000
set -e
W=$(cd "$(dirname "$0")" && pwd)
UA=$(cd "$W/../../../.." && pwd)	# usr-amix, above amix-userland
XC_SRC=${XC_SRC:-$UA/usr/x11r6/xc}
RTG=${RTG:-$UA/upstream/amix-z3660rtg}
DISTFILES=${DISTFILES:-$W/distfiles}
HDF=${HDF:-"$HOME/Documents/FS-UAE/Hard Drives/hd0Z3660AMIX_AMIGA3000.hdf"}
SYSROOT=$HOME/opt/amix-cross/m68k-cbm-sysv4/sysroot
mkdir -p "$W/log"

# 1. the tree: a copy, CRLF in the AMIX DDX converted, the fork's patches in order
rm -rf "$W/src/xc"; mkdir -p "$W/src"; cp -a "$XC_SRC" "$W/src/"
cd "$W/src/xc"
grep -rlI "$(printf '\r')" programs/Xserver/hw/amix > "$W/log/crlf-files.txt" || true
[ -s "$W/log/crlf-files.txt" ] && xargs sed -i 's/\r$//' < "$W/log/crlf-files.txt"
for p in zz9000hw-modes p5-1-solidrect-alu p5-2-copyarea-expose p5-3-setspans-alu \
    p5-4-copyarea-alu p5-5-copyarea-onebox p5-6-stores32 p5-7-shortruns \
    rt04-monsw-hook rt07-card-blitter rt07b-small-fills rt07c-zeroline-bias \
    rt08-banded-default rt10-console-keyboard; do
	patch -p1 --no-backup-if-mismatch < "$RTG/x11/patches/$p.patch" > "$W/log/patch-$p.log"
done
# local deviation: the per-GC trace lines only with -DZZ9000_DEBUG
python3 - programs/Xserver/hw/amix/rtg/zz9000/zz9000screen.c <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
for a in ['    ErrorF("ZZ9000CreateGC: depth=%d\\n", pGC->depth);\n',
          '    ErrorF("ZZ9000CreateGC: pPriv=%p\\n", pGC->devPrivates[rtgGCPrivateIndex].ptr);\n']:
    assert s.count(a) == 1, a
    s = s.replace(a, "#ifdef ZZ9000_DEBUG\n" + a + "#endif\n")
open(p, "w").write(s)
PY
# local fix: CopyWindow chose the box order from old - new the wrong way round
# (smeared partly covered windows on every move); same walk as CopyArea now
patch --no-backup-if-mismatch programs/Xserver/hw/amix/rtg/zz9000/zz9000draw.c \
	< "$W/local-copywindow-order.patch" > "$W/log/patch-local-copywindow-order.log"
# local fix: PaintWindow filled pixmap backgrounds and borders with the tile's
# first pixel ("not yet tiled"), so every CDE backdrop came out a plain colour
patch --no-backup-if-mismatch programs/Xserver/hw/amix/rtg/zz9000/zz9000draw.c \
	< "$W/local-paintwindow-tile.patch" > "$W/log/patch-local-paintwindow-tile.log"
# local fix: FillTiled/FillStippled/FillOpaqueStippled spans were solid fills
# ("TODO"), so tiled and stippled rectangles (dtstyle's backdrop preview,
# Motif's insensitive shading) came out as plain blocks
patch -p1 --no-backup-if-mismatch < "$W/local-pattern-fill.patch" > "$W/log/patch-local-pattern-fill.log"
# local fix: amixBell and amixKbdCtrl were empty stubs, so X never beeped (CDE's
# Beep settings, ^G); now a square wave on /dev/audio, played by a grandchild
patch -p1 --no-backup-if-mismatch < "$W/local-bell.patch" > "$W/log/patch-local-bell.log"
# local change: PutImage of a 16-bit ZPixmap under GXcopy straight into the
# drawable (no mi span list), the aligned middle of each row by move16 --
# MIT-SHM's swap of a 512x320 Mesa image went from 43 ms toward 13 ms
patch -p1 --no-backup-if-mismatch < "$W/local-putimage-move16.patch" > "$W/log/patch-local-putimage-move16.log"
# local change: the ZZ9K-SOFT3D extension -- a 3D client's off-screen VRAM
# frame copied into its window through the clip list by the card's blitter
# (TinyGL on the Z3660's soft3D), so menus and overlapping windows survive
patch -p1 --no-backup-if-mismatch < "$W/local-soft3d-swap.patch" > "$W/log/patch-local-soft3d-swap.log"

# local change: RandR 1.1, a live resolution change (xrandr, the CDE display
# panel).  The DIX extension is xorg-server 1.0.2's randr/ and the headers are
# randrproto 1.1.2, both pristine from the tarballs; local-randr.patch fits
# randr.c to the R6.3 DIX, registers the extension, adds ScreenRestructured
# to dix/events.c and the DDX side (rtg/rtgRandR.c, ZZ9000SetSize)
(cd "$DISTFILES" && sha256sum -c "$W/SOURCES.sha256") > "$W/log/randr-sha256.log"
rm -rf "$W/work-randr"; mkdir -p "$W/work-randr"
tar -xjf "$DISTFILES/xorg-server-1.0.2.tar.bz2" -C "$W/work-randr" \
	xorg-server-1.0.2/randr/randr.c xorg-server-1.0.2/randr/randrstr.h
tar -xjf "$DISTFILES/randrproto-1.1.2.tar.bz2" -C "$W/work-randr" \
	randrproto-1.1.2/randr.h randrproto-1.1.2/randrproto.h
mkdir -p programs/Xserver/randr
cp "$W/work-randr/xorg-server-1.0.2/randr/randr.c" \
	"$W/work-randr/xorg-server-1.0.2/randr/randrstr.h" programs/Xserver/randr/
cp "$W/work-randr/randrproto-1.1.2/randr.h" \
	"$W/work-randr/randrproto-1.1.2/randrproto.h" include/extensions/
rm -rf "$W/work-randr"
patch -p1 --no-backup-if-mismatch < "$W/local-randr.patch" > "$W/log/patch-local-randr.log"
# local change: RandR rates from the kernel's table (GETMODES/SETMODEHZ), so
# that the display panel offers 1080p at 50 Hz too; 60 alone on older kernels
patch -p1 --no-backup-if-mismatch < "$W/local-randr-rates.patch" > "$W/log/patch-local-randr-rates.log"

# local change: MIT-SHM on (Mesa's back buffer, x11perf -shmput); the port
# kernel's ISSUE-62/66 fixes made shmget/shmat safe, shmband-tested 2026-10-25
sed -i 's|^/\* MIT-SHM is independent of shared libraries and is unsafe on AMIX. \*/$|/* MIT-SHM: safe since the port kernel'"'"'s ISSUE-62/66 shm fixes */|; s|^#define HasShm NO$|#define HasShm YES|' config/cf/amix.cf
grep -q '^#define HasShm YES$' config/cf/amix.cf

# 2. the mirror sysroot: the toolchain's plus /usr/amiga, libscreen.so, libdbm.a
if [ ! -f "$W/disk/usr/amiga/lib/libscreen.so" ]; then
	for d in /usr/amiga/include /usr/amiga/lib /usr/ucblib; do
		python3 -I "$W/ufsget.py" "$HDF" 128 $d "$W/disk$d"
	done
fi
M=$W/sysroot-mirror
rm -rf "$M"; cp -rs "$SYSROOT" "$M"
rm "$M/usr/lib/libscreen.so" "$M/usr/ccs/lib/libdbm.a" "$M/usr/include/amiga"
cp "$W/disk/usr/amiga/lib/libscreen.so" "$M/usr/lib/libscreen.so"
cp "$W/disk/usr/ucblib/libdbm.a" "$M/usr/ccs/lib/libdbm.a"
cp -r "$W/disk/usr/amiga/include/amiga" "$M/usr/include/amiga"
mkdir -p "$M/usr/amiga" "$M/usr/ucblib"
cp -r "$W/disk/usr/amiga/include" "$W/disk/usr/amiga/lib" "$M/usr/amiga/"
cp "$W/disk/usr/ucblib/"*.a "$M/usr/ucblib/"

# 3. the stub archive: libc.a members whose plain names libc.so.1 does not export
export PATH=$HOME/opt/amix-cross/bin:$PATH
rm -rf "$W/stub"; mkdir -p "$W/stub"; cd "$W/stub"
MEMBERS="fpsetmask.o fpsetrnd.o fpsetsticky.o gettimeofday.o hrtsys.o select.o
	seteguid.o shmsys.o signal.o syscall.o syslog.o time.o vfork.o"
m68k-cbm-sysv4-ar x "$M/usr/ccs/lib/libc.a" $MEMBERS
m68k-cbm-sysv4-ar rcs libsockstub.a $MEMBERS

# 4. host.def, site.def, host imake and cpp wrapper
cd "$W/src/xc"
{ printf '#define AmixSysroot %s\n#define AmixXStubDir %s\n' "$M" "$W/stub"
  cat "$RTG/x11/cross/host.def"; } > config/cf/host.def
sed -i '127s|^/\* #include <host.def> \*/$|#include <host.def>|' config/cf/site.def
cp "$RTG/x11/cross/imakecpp.sh" "$W/imakecpp.sh"; chmod +x "$W/imakecpp.sh"
(cd config/imake && make -f Makefile.ini BOOTSTRAPCFLAGS="-DAMIX -DSVR4 -Dm68k" \
	CC="gcc -std=gnu89 -fcommon") > "$W/log/01-imake-bootstrap.log" 2>&1
export AMIX_SYSROOT=$M IMAKECPP=$W/imakecpp.sh
make xmakefile > "$W/log/02-xmakefile.log" 2>&1
make -f xmakefile Makefiles > "$W/log/03-Makefiles.log" 2>&1
make -f xmakefile includes > "$W/log/04-includes.log" 2>&1 || true	# client-lib host tools fail; not needed

# 5. the server's libraries, lbxutil's generated header (host tool), the server
for d in lib/Xau lib/Xdmcp lib/xkbfile lib/font lib/zlib; do
	(cd $d && make) > "$W/log/05-$(basename $d).log" 2>&1
done
gcc -std=gnu89 -fcommon -w -o "$W/mkg3states-host" lib/lbxutil/image/mkg3states.c
"$W/mkg3states-host" > lib/lbxutil/image/g3states.h
(cd lib/lbxutil && make) > "$W/log/08-lbxutil.log" 2>&1
(cd programs/Xserver && make Xzz9000) > "$W/log/09-Xserver.log" 2>&1
ls -l programs/Xserver/Xzz9000
sha256sum programs/Xserver/Xzz9000
