# X11R6.3 for AMIX

An independent X11R6.3 port for Commodore Amiga UNIX (AMIX), targeting the
MNT ZZ9000 graphics card through the AMIX `/dev/zz9000` framebuffer driver.

The repository contains only the AMIX port overlay. The unmodified X11R6.3
sources are downloaded from the official X.Org archive by `install.sh`.

## Current status

- X11R6.3 builds with GCC 2.7.2.3 and the AMIX GNU preprocessor.
- The `Xzz9000` server links and starts at 16-bit modes including 1920x800.
- The ZZ9000 framebuffer uses R6 pixmap-private storage correctly for both
  16-bit drawing and depth-1 font/cursor bitmaps.
- Shared libraries use the native AMIX model: `gcc -fpic`, ELF runtime
  images linked by `ld -G -h`, and companion `.sa` stub archives for Xt
  and Xmu. Xaw has no `.sa` archive.
- MIT-SHM is off by default because it is not safe on the stock AMIX kernel.
  On the 68040/68060 port kernel it is (see "The Z3660 and the A3000" below).
- Dynamic `xclock`, `xterm`, and `twm` are the current validation targets.
- There is no GLX. OpenGL programs use Mesa's Xlib driver
  ([mesa-amix](https://github.com/isoriano1968/mesa-amix)).

This is development software. Keep the existing X11R4 and X11R5 installations
available; the port installs under `/usr/x11r6` and does not replace them.

## Prepare the source tree

Run this on a system with HTTPS access, `gzip`, `tar`, and a SHA-256 tool:

```sh
git clone https://github.com/isoriano1968/x11r6.3-amix.git
cd x11r6.3-amix
sh install.sh /export/amix/x11r6.3
```

The result is `/export/amix/x11r6.3/xc`. Copy or mount that tree as
`/usr/x11r6/xc` on AMIX.

The installer downloads all three official source archives, verifies their
SHA-256 hashes, extracts them into one `xc` tree, and applies `overlay/xc`.

## AMIX prerequisites

- AMIX 2.1 / SVR4 on m68k
- GCC 2.7.2.3 available as `gcc`
- GNU cpp at `/usr/public/lib/gcc-cpp`
- AMIX platform headers under `/usr/amiga/include`
- ZZ9000 kernel driver and `/dev/zz9000`
- System libraries `socket`, `nsl`, `dbm`, and `screen`

Do not substitute the Solaris NFS server compiler for the AMIX compiler.

## Shared-library rebuild

After generating the X11R6 Makefiles on AMIX, rebuild the runtime libraries
and initial clients with:

```sh
cd /usr/x11r6/xc
sh config/util/amix-shared-rebuild.sh /usr/x11r6/xc libs \
  >/tmp/x11r6-shared.log 2>&1
```

Use mode `all` when configuration changed and every Makefile must be
regenerated. Mode `clients` validates existing libraries and only relinks
the test-session programs: `xinit`, `xsetroot`, `xclock`, `xterm`, and `twm`.

Mode `shdata` regenerates the Makefiles and rebuilds only Xt, Xmu, and Xaw
with the AMIX/X11R5 shared-data ABI. It is the migration path from an older
port checkout that already has valid X11, Xext, ICE, and SM shared objects.

The script deliberately removes old library objects before rebuilding. Static
and PIC objects must never be mixed on AMIX.

AMIX library ordering and the distinction between ELF `.so` images and `.sa`
stub archives are not optional details. See [SHARED-LIBRARIES.md](SHARED-LIBRARIES.md)
before changing library rules or client link lines.

To build additional X11R6 client programs after the shared libraries are
valid, use the resumable program helper:

```sh
cd /usr/x11r6/xc
make -f xmakefile Makefiles
sh config/util/amix-build-programs.sh /usr/x11r6/xc useful \
  >/tmp/x11r6-programs.log 2>&1
```

Mode `common` builds the broader historical client set. Mode `all` also tries
optional clients that may still need AMIX-specific attention. By default the
script keeps going and reports all failed clients at the end; set
`AMIX_PROGRAMS_KEEP_GOING=no` to stop on the first failure.

## Running Xzz9000

The included `xinit` wrapper starts a 1920x800 session with `twm`, `xterm`,
and `xclock` from the build tree:

```sh
cd /usr/x11r6/xc
sh config/util/startXzz9000 :0
```

Choose `Exit` from the twm root menu to terminate the session and X server.
Use `:1` instead if display `:0` is occupied. The launcher supports
`ZZ9K_MODE`, `ZZ9K_FONT_PATH`, and `X11R6_TOP` environment overrides.

The shipped `twmrc.zz9000` adds root-menu entries for `xterm`, `xclock`, root
background refresh, and the Mesa gears demo. The gears menu entry looks first
for `/usr/mesa3/Mesa-3.1/demos/gears` and then for
`/usr/mesa/Mesa3.1/demos/gears`.

The session loads `/usr/x11r6/xc/config/util/Xmodmap.de` by default when
`xmodmap` is available. Override or disable it with `ZZ9K_XMODMAP`:

```sh
ZZ9K_XMODMAP=none sh config/util/startXzz9000 :1
ZZ9K_XMODMAP=/path/to/Xmodmap sh config/util/startXzz9000 :1
```

Set `ZZ9K_SHELL` to choose the shell used by the initial xterm and by the twm
menu xterm entry:

```sh
ZZ9K_SHELL=/usr/public/bin/bash sh config/util/startXzz9000 :1
```

The launcher uses `-ac` for the initial local test and `-kb` to avoid the
unfinished XKB device path. Do not expose this test server to an untrusted
network.

## The Z3660 and the A3000

The ZZ9000 server also drives the Z3660 accelerator's RTG output, whose
registers are ZZ9000-compatible. The server sources in this overlay include
two patch series:

- **Jusii's** from [amix-z3660rtg](https://github.com/jusii/amix-z3660rtg):
  - the Z3660's video modes;
  - correctness fixes for raster ops and window copies;
  - fills and copies on the card's blitter;
  - the monitor switch;
  - the keyboard hand-back to the console when the server ends.
- **The ones from the A3000 + Z3660 68060 setup**
  ([soft3d-z3660rtg-amix](https://github.com/isoriano1968/soft3d-z3660rtg-amix)):
  - correct CopyWindow box order, tiled window backgrounds, and tiled and
    stippled fills, which CDE and Motif need;
  - the keyboard bell on `/dev/audio`;
  - a direct 16-bit PutImage;
  - the ZZ9K-SOFT3D extension, which shows frames that the Z3660's soft3D
    rasterizer drew in off-screen VRAM. It's only registered when the kernel
    driver offers `ZZ9KIOC_BLITX`.

Two settings matter on a 68040/68060 kernel from the
[68040/68060 port](https://github.com/asokero/amix-040-060-port):

- `-move16` (server option): PutImage copies rows with the 68040/68060's
  `move16` (26 MB/s into VRAM instead of 10.6). Off by default, because a
  68030 has no `move16`.
- `#define AmixHasShm YES` in `config/cf/site.def`: builds MIT-SHM in. The
  port kernel's shared-memory fixes make it safe; Mesa's back buffer and
  `x11perf -shmput` use it.

The CDE desktop and Motif for this X11R6.3 are in
[cde-amix](https://github.com/isoriano1968/cde-amix).

## Source and licensing

X11R6.3 was released by the X Consortium. Its original source files retain
their own notices. AMIX glue derived from historical X11 code also retains
the original University of California, Sun Microsystems, MIT, and other
notices. New project material is covered by `LICENSE`.

## References

- [X.Org X11R6.3 archive](https://www.x.org/archive/X11R6.3/)
- [VA2000 AMIX device driver](https://github.com/asokero/va2000-amix)
- [VA2000 AMIX X11R5 RTG driver](https://github.com/asokero/xrtg-amix)
- [MNT ZZ9000 drivers and hardware definitions](https://github.com/BlitterStudio/zz9000-drivers)
