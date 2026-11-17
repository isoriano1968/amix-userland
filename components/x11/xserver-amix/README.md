# x11/xserver-amix: Xamix, the AMIX X server

`Xamix` is the X11R6.3 X server for AMIX (DDX `programs/Xserver/hw/amix`).
It is one server for every graphics card; the cards are drivers inside it,
named after the card:

| driver | cards | kernel device | depths |
|---|---|---|---|
| `z3660` | Z3660 RTG | `/dev/z3660rtg` (major 55, amix-gate io/z3660rtg); else `/dev/zz9000` (major 49) if the board there is a Z3660 (the z3660 kernel variant's built-in RTG) | 16 |
| `zz9000` | MNT ZZ9000 (a Z3660 is refused: use `z3660`) | `/dev/zz9000` (major 49) | 16, 24 (32-bit pixels) |
| `cirrus` | Village Tronic Picasso II/II+ (GD542x) | `/dev/rtgfb` (major 54) | 8 |
| `va2000` | later | | |

`-card NAME` or `/etc/X11/amix-card` picks the driver, else the server probes
them in this order (docs/MODULAR-X-SERVER-EVALUATION.md); a card file whose
card finds no board falls back to the probe.  `-device PATH` replaces the
node of the card `-card` (or the card file) names.  The name follows the X
convention of one server per platform (Xsun, Xhp).

**Depths.**  `-depth N` (or `-rtgdepth N`; 8, 15, 16, 24, or 32 for 24 in
32-bit pixels), else the card file's second word, else 16.  The z3660 and
zz9000 cards ask the kernel driver (`RTGIOC_GETCAPS`, amix-gate
io/rtgcaps.h, copied byte for byte into rtg/rtgcaps.h) which depths the board
shows and offer those they draw: the Z3660's HDMI is RGB565, its modes are 8,
15 and 16 bit, and X gets 16 (8 waits for a colour table op, 15 for a 15-bit
layer); `-depth 24` or `32` there stops the server with the board's depths.
A kernel without the ioctl (EINVAL, ENOTTY) gets the old static facts: Z3660
16, ZZ9000 16 and 24.  RandR takes its sizes from the kernel's mode list
(`RTGCAP_MODE_LIST`) and offers rates only with `RTGCAP_REFRESH`.

**Old name.** Until 2026-11-16 the server was `Xzz9000` and this component
was `x11/xzz9000`.  The evaluations in `docs/`, the benchmarks in `bench/`
and measurement notes elsewhere keep `Xzz9000` where they describe what was
measured or built then.  The AMIX material the build reads and writes stays
in `private/xzz9000/` (`src`, `disk`, `log`, `sysroot-mirror` are links to
it).

## Build

    sh build.sh          (or: make build)

The tree is a copy of the R6.3 port tree (`XC_SRC`), the RTG fork's patches,
then `local-*.patch` in the order build.sh gives.  The RTG fork's X material
is in the component, unchanged from amix-z3660rtg 1.0 (`91667de`, the same
archive as x11/x11r6.3's `RTG_COMMIT`): its `x11/patches` in
`patches/amix-z3660rtg/`, `x11/cross/host.def` in `files/`,
`x11/cross/imakecpp.sh` as `imakecpp.sh`; `patches/amix-z3660rtg.sha256`
holds their sums, build.sh checks them.  No outside checkout is read.

The server target is `Xamix` in `programs/Xserver/Imakefile` (x11r6.3's
`0001-amix-port.patch`); build.sh renames it in a tree made before the
rename.  Output: `src/xc/programs/Xserver/Xamix`.  Version 1's only
difference to the last Xzz9000 build was the name (same sha256,
876470510f35...; the imported patches give it too).

## On the machine

| file | what |
|---|---|
| `/usr/x11r6/bin/Xamix` | the server |
| `/usr/x11r6/bin/X` | link to `Xamix` |
| `/opt/amix/bin/Xamix-dt` | the server as dtlogin starts it: limits, library path, `-mode` from `/etc/dt/config/rtg-mode`, fonts; log `/var/dt/Xamix.log` (source: desktop/cde `files/dtlogin/`) |
| `/etc/dt/config/Xservers` | `:0 Local local_uid@console root /opt/amix/bin/Xamix-dt :0` |

Stock AMIX's X11R5 server stays in `/usr/X/bin` and does not collide.

## Switching an existing system from Xzz9000

A system installed before 2026-11-16 has `/opt/amix/bin/Xzz9000` and
`/opt/amix/bin/Xzz9000-dt`, and its `Xservers` names `Xzz9000-dt`.  They
keep working until the new package is installed; the update does, in order:

1. install `Xamix` as `/usr/x11r6/bin/Xamix.new`, check its sha256, `mv` it
   into place, then the `X` link;
2. install `Xamix-dt` the same way, then rewrite `/etc/dt/config/Xservers`
   to name it (desktop/cde `dtlogin-install.sh` does steps 2-3 and refuses
   to run without `/usr/x11r6/bin/Xamix`);
3. remove `/opt/amix/bin/Xzz9000-dt` and `/opt/amix/bin/Xzz9000`.  No
   backup copies: to go back, rebuild the old version from git.

The running server keeps running; the new one starts with the next X start
(`/etc/init.d/dtlogin stop` and `start`, or a reboot).  Tools that
look for the old names must change too: amixdisplay's docs, the AMIX
Installer.
