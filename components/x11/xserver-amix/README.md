# x11/xserver-amix: Xamix, the AMIX X server

`Xamix` is the X11R6.3 X server for AMIX (DDX `programs/Xserver/hw/amix`).
It is one server for every graphics card; the cards are drivers inside it,
named after the card:

| driver | cards | kernel device |
|---|---|---|
| `zz9000` | MNT ZZ9000, Z3660 RTG | `/dev/zz9000` (major 49) |
| `cirrus` | Village Tronic Picasso II/II+ (GD542x) | `/dev/rtgfb` (major 54) |
| `va2000` | later | |

`-card NAME` or `/etc/X11/amix-card` picks the driver, else the server probes
them in order (docs/MODULAR-X-SERVER-EVALUATION.md).  The name follows the X
convention of one server per platform (Xsun, Xhp).

**Old name.** Until 2026-11-16 the server was `Xzz9000` and this component
was `x11/xzz9000`.  The evaluations in `docs/`, the benchmarks in `bench/`
and measurement notes elsewhere keep `Xzz9000` where they describe what was
measured or built then.  The AMIX material the build reads and writes stays
in `private/xzz9000/` (`src`, `disk`, `log`, `sysroot-mirror` are links to
it).

## Build

    sh build.sh          (or: make build)

The tree is a copy of the R6.3 port tree (`XC_SRC`), the RTG fork's patches,
then `local-*.patch` in the order build.sh gives.  The server target is
`Xamix` in `programs/Xserver/Imakefile` (x11r6.3's `0001-amix-port.patch`);
build.sh renames it in a tree made before the rename.  Output:
`src/xc/programs/Xserver/Xamix`.  The name is the only difference to the
last Xzz9000 build: both binaries have the same sha256 (876470510f35...).

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
