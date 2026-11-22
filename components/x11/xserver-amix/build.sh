#!/bin/sh
# build.sh -- cross-build the X11R6.3 X server Xamix (hw/amix: one server, the
# graphics cards are drivers in it: z3660, zz9000, cirrus) for the A3000 + Z3660
# RTG (amix-z3660rtg docs/X-CROSS.md), from the local copy of isoriano's
# X11R6.3 tree.  Every step logs to log/.
# The RTG fork's X material is part of this component, unchanged from
# amix-z3660rtg 1.0 (91667dec985e, the archive of x11/x11r6.3's RTG_COMMIT,
# sha256 3b41d6bd...): its x11/patches in patches/amix-z3660rtg/, its
# x11/cross/host.def in files/, its x11/cross/imakecpp.sh as ./imakecpp.sh;
# patches/amix-z3660rtg.sha256 holds their sums.  No outside checkout is read.
#   sh build.sh            full build from a fresh copy of the tree
# Inputs:
#   XC_SRC   an X11R6.3 port tree to copy (never edited); default: a fresh
#            one from x11/x11r6.3's install.sh (the pristine xc-1/2/3 archives
#            and its port patch, in DISTFILES)
#   DISTFILES  xorg-server-1.0.2 and randrproto-1.1.2 tarballs (default ./distfiles,
#            checked against SOURCES.sha256; the Makefile fetches them)
#   GCC27_TOOLS  gcc 2.7.2.3 for AMIX (developer/amix/gcc-27), default
#            ~/opt/amix-cross/bin; its sysroot is the AMIX sysroot
#   HDF      only for a sysroot without /usr/amiga: an AMIX disk image for
#            /usr/amiga and /usr/ucblib (read-only)
# Output: src/xc/programs/Xserver/Xamix (Xzz9000 before 2026-11-16)
set -e
W=$(cd "$(dirname "$0")" && pwd)
# until the local patches are rebased onto x11/x11r6.3's port patch (it already
# holds 11 of them and parts of 12 others: README.md), the default tree is the
# port tree they were made on, where it is (usr-amix/usr/x11r6/xc)
[ -z "$XC_SRC" ] && [ -d "$W/../../../../usr/x11r6/xc" ] && XC_SRC=$(cd "$W/../../../../usr/x11r6/xc" && pwd)
DISTFILES=${DISTFILES:-$W/distfiles}
GCC27_TOOLS=${GCC27_TOOLS:-$HOME/opt/amix-cross/bin}
SYSROOT=$(readlink -f "$GCC27_TOOLS/../m68k-cbm-sysv4/sysroot")
mkdir -p "$W/log"
(cd "$W" && sha256sum -c patches/amix-z3660rtg.sha256) > "$W/log/rtg-sha256.log"

# 1. the tree: a copy, CRLF in the AMIX DDX converted, the fork's patches in order
rm -rf "$W/src/xc"; mkdir -p "$W/src"
if [ -n "$XC_SRC" ]; then
	cp -a "$XC_SRC" "$W/src/"
else
	X11R63_DISTFILES=$DISTFILES sh "$W/../x11r6.3/install.sh" "$W/src" > "$W/log/00-install.log" 2>&1
fi
cd "$W/src/xc"
# the server target is Xamix (x11r6.3's port patch since 2026-11-16); a tree
# made from the port patch before then names it Xzz9000.  The local patches
# below expect Xamix in programs/Xserver/Imakefile.
sed -i 's/Xzz9000/Xamix/g' programs/Xserver/Imakefile
grep -q '^ServerTarget(Xamix,' programs/Xserver/Imakefile
if grep -q Xzz9000 programs/Xserver/Imakefile; then echo "Imakefile: Xzz9000 left" >&2; exit 1; fi
grep -rlI "$(printf '\r')" programs/Xserver/hw/amix > "$W/log/crlf-files.txt" || true
[ -s "$W/log/crlf-files.txt" ] && xargs sed -i 's/\r$//' < "$W/log/crlf-files.txt"
for p in zz9000hw-modes p5-1-solidrect-alu p5-2-copyarea-expose p5-3-setspans-alu \
    p5-4-copyarea-alu p5-5-copyarea-onebox p5-6-stores32 p5-7-shortruns \
    rt04-monsw-hook rt07-card-blitter rt07b-small-fills rt07c-zeroline-bias \
    rt08-banded-default rt10-console-keyboard; do
	patch -p1 --no-backup-if-mismatch < "$W/patches/amix-z3660rtg/$p.patch" > "$W/log/patch-$p.log"
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
# local fix: AMIX's AF_UNIX socket file (libsocket over ticots) is a FIFO, as
# on Sun SVR4; xtrans expected S_IFSOCK, so every server reset (the last
# client gone) closed and re-made the UNIX listener, and a client connecting
# in that moment was refused: dthello's second XOpenDisplay, quick runs of
# clients ("unable to open display", "can't open display")
patch -p1 --no-backup-if-mismatch < "$W/local-xtrans-unix-fifo.patch" > "$W/log/patch-local-xtrans-unix-fifo.log"

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

# local change: the RTG core split from the card (step 1 of
# docs/MODULAR-X-SERVER-EVALUATION.md).  rtg.h gets a card ops table
# (rtgCardRec: probe, initHW, getModes, setMode, closeHW, monitorSwitch,
# setColors, solidFill/copyRect/patternFill/waitIdle, HW cursor, extensions,
# caps) and a versioned callback table (rtgCoreFuncsRec) that is a card's
# only way into the server; rtgInit.c probes a static card list (-card NAME,
# /etc/X11/amix-card).  Everything card-independent moves from rtg/zz9000/
# into the core (rtgdraw.c, rtgwin.c, rtgscreen.c, rtgcmap.c, rtgpix.c):
# CopyWindow box order, PaintWindow tiles, pattern spans, the 16-bit PutImage
# rows by move16 where uname names a 68040/060 (rtgcpu.c; never executed to
# probe), else by movem.l blocks, the blitter banding (-blitmin/-blitmax).
# Depths 8 (cfb, rtgcfb8.c), 16 (the core's own layer) and 24 in 32-bit
# pixels (cfb32, rtgcfb32.c; cfb32 now built); -rtgdepth or the card file.
# The zz9000 card keeps probe (Z3660: 16; MNT ZZ9000: 16 and 24), modes, its
# BLITS queue and ZZ9K-SOFT3D
patch -p1 --no-backup-if-mismatch < "$W/local-rtg-cardops.patch" > "$W/log/patch-local-rtg-cardops.log"
# local change: the second card, `cirrus' (rtg/cirrus/): the Cirrus GD542x of
# the Village Tronic Picasso II/II+ (Zorro II, real and in FS-UAE), through
# the generic rtgfb kernel driver (/dev/rtgfb, major 54, amix-gate
# io/rtgfb): VGA/Cirrus mode setting with VCLK3 for 640x480, 800x600 and
# 1024x768 at 8 bits and their RandR rates, the 6-bit DAC, the BitBLT engine
# for solid fills (colour-expanded pattern) and copies, the monitor switch
# by the SVGAIOC contract.  Depth 8 only (16-bit pixels are little-endian
# on this chip); the core's software cursor (FS-UAE draws no GD542x cursor)
patch -p1 --no-backup-if-mismatch < "$W/local-rtg-cirrus.patch" > "$W/log/patch-local-rtg-cirrus.log"
# local change: the Z3660 RTG as its own card, `z3660' (amix-gate 82bd7df:
# io/z3660rtg, /dev/z3660rtg major 55, the zz9000 ioctls; io/rtgcaps.h).
# Card z3660 opens /dev/z3660rtg, else /dev/zz9000 if the board there is a
# Z3660 (the z3660 kernel variant's built-in RTG); card zz9000 is the MNT
# ZZ9000 on /dev/zz9000 and refuses a Z3660 with a word to use z3660;
# -device PATH replaces the card's node.  The depths come from
# RTGIOC_GETCAPS (rtg/rtgcaps.h, the kernel's block byte for byte), the old
# static facts on a kernel without it: the Z3660 16 only (its HDMI is
# RGB565), the ZZ9000 16 and 24.  -depth N (8, 15, 16, 24, 32 = 24 in
# 32-bit pixels) is the RTG depth, as -rtgdepth; one the board cannot show
# stops the server with its depths.  RandR: the kernel's mode list as the
# sizes with RTGCAP_MODE_LIST, rates only with RTGCAP_REFRESH.  The cirrus
# card takes -device too.  rtgCoreFuncsRec grows .device: RTG_CORE_VERSION 2
patch -p1 --no-backup-if-mismatch < "$W/local-rtg-z3660.patch" > "$W/log/patch-local-rtg-z3660.log"

# local change: MIT-SHM on (Mesa's back buffer, x11perf -shmput); the port
# kernel's ISSUE-62/66 fixes made shmget/shmat safe, shmband-tested 2026-10-25
sed -i 's|^/\* MIT-SHM is independent of shared libraries and is unsafe on AMIX. \*/$|/* MIT-SHM: safe since the port kernel'"'"'s ISSUE-62/66 shm fixes */|; s|^#define HasShm NO$|#define HasShm YES|' config/cf/amix.cf
grep -q '^#define HasShm YES$' config/cf/amix.cf

# 2. the mirror sysroot: the toolchain's plus /usr/amiga, libscreen.so, libdbm.a
# (the AMIX sysroot has them: developer/amix/sysroot; else from an HDF)
D=$SYSROOT
if [ ! -f "$D/usr/amiga/lib/libscreen.so" ]; then
	D=$W/disk
	[ -f "$D/usr/amiga/lib/libscreen.so" ] || for d in /usr/amiga/include /usr/amiga/lib /usr/ucblib; do
		python3 -I "$W/ufsget.py" "$HDF" 128 $d "$D$d"
	done
fi
# sysroot-mirror is a link into private/ (AMIX material): rebuild its target,
# not the link
M=$W/sysroot-mirror
[ -L "$M" ] && M=$(readlink -f "$M")
rm -rf "$M"; cp -rs "$SYSROOT" "$M"
rm "$M/usr/lib/libscreen.so" "$M/usr/ccs/lib/libdbm.a" "$M/usr/include/amiga"
cp "$D/usr/amiga/lib/libscreen.so" "$M/usr/lib/libscreen.so"
cp "$D/usr/ucblib/libdbm.a" "$M/usr/ccs/lib/libdbm.a"
cp -r "$D/usr/amiga/include/amiga" "$M/usr/include/amiga"
mkdir -p "$M/usr/amiga" "$M/usr/ucblib"
cp -r "$D/usr/amiga/include" "$D/usr/amiga/lib" "$M/usr/amiga/"
cp "$D/usr/ucblib/"*.a "$M/usr/ucblib/"

# 3. the stub archive: libc.a members whose plain names libc.so.1 does not export.
# The list is stub-members.txt (one member per line); stub/ is build output only.
export PATH=$GCC27_TOOLS:$PATH
rm -rf "$W/stub"; mkdir -p "$W/stub"; cd "$W/stub"
MEMBERS=$(cat "$W/stub-members.txt")
m68k-cbm-sysv4-ar x "$M/usr/ccs/lib/libc.a" $MEMBERS
m68k-cbm-sysv4-ar rcs libsockstub.a $MEMBERS

# 4. host.def, site.def, host imake and cpp wrapper
cd "$W/src/xc"
# UseRgbTxt: the colour names from rgb.txt (this package's
# /usr/x11r6/lib/X11/rgb.txt), not an ndbm database: until 2026-11-20 the
# server read stock AMIX's /usr/X/lib/rgb.dir/.pag (Commodore's Xcore), which
# an AMIX without Xcore lacks
{ printf '#define AmixSysroot %s\n#define AmixXStubDir %s\n#define UseRgbTxt YES\n' "$M" "$W/stub"
  cat "$W/files/host.def"; } > config/cf/host.def
sed -i '127s|^/\* #include <host.def> \*/$|#include <host.def>|' config/cf/site.def
chmod +x "$W/imakecpp.sh"
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
(cd programs/Xserver && make Xamix) > "$W/log/09-Xserver.log" 2>&1
ls -l programs/Xserver/Xamix
sha256sum programs/Xserver/Xamix
