# X11R6.3 for AMIX

An independent X11R6.3 port for Commodore Amiga UNIX (AMIX), targeting the
MNT ZZ9000 graphics card through the AMIX `/dev/zz9000` framebuffer driver.

The repository contains only the AMIX port overlay. The unmodified X11R6.3
sources are downloaded from the official X.Org archive by `install.sh`.

## Current status

- X11R6.3 builds with GCC 2.7.2.3 and the AMIX GNU preprocessor.
- The `Xzz9000` server links and starts at 16-bit modes including 1920x800.
- The software cursor and ZZ9000 framebuffer are operational.
- Shared libraries use the working X11R5 AMIX model: `gcc -fpic`, `ld -G`,
  and per-executable data archives for Xt, Xmu, and Xaw.
- MIT-SHM is disabled independently because it is not safe on AMIX.
- Dynamic `xclock`, `xterm`, and `twm` are the current validation targets.
- GLX and Mesa are not included yet.

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

The launcher uses `-ac` for the initial local test and `-kb` to avoid the
unfinished XKB device path. Do not expose this test server to an untrusted
network.

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
