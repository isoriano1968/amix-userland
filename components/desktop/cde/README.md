# CDE for AMIX

Build scripts and patches for the **Common Desktop
Environment 2.2.4a** on **AMIX 2.1** (Commodore's Amiga UNIX, SVR4.0),
cross-built on a Linux host with
[gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix).

The result is the full CDE desktop on an Amiga: dtlogin, dtsession, dtwm
with the front panel, dtterm, the file manager, dtpad, the style manager,
the help viewer, dtcalc, dtcm, dtmail and the rest of `/usr/dt/bin`. Motif and `mwm` are a component of their own,
[desktop/motif](../motif): build that first (see `cde.manifest`).

This repository contains **no binaries and no AMIX material**. You build
everything yourself from the original CDE release, the patches
here, and your own AMIX installation.

## Requirements

### The Amiga

- **A 68040 or 68060 CPU.** gcc14-cross-amix builds for the 68060, including
  the 68040/68060 floating-point instructions a 68881/68882 does not have.
  The binaries do not run on a 68030 (an Amiga 3000's own CPU).
- **AMIX 2.1 with a 68040/68060 kernel** (the
  [amix-040-060-port](https://github.com/asokero/amix-040-060-port)).
- **X11R6.3 for AMIX** ([x11r6.3-amix](https://github.com/isoriano1968/amix-userland/tree/main/components/x11/x11r6.3)),
  built and installed on the machine, and an X server for your display.
- About 200 MB of free disk space under `/usr/dt`.

Developed and tested on an Amiga 3000 with a Z3660 (68060 at 80 MHz, 128 MB),
the Z3660's RTG output and the `Xzz9000` X server
([amix-z3660rtg](https://github.com/jusii/amix-z3660rtg)).

### The build host

- Linux with `gcc`, `g++`, `make`, `patch`, `flex`, `python3`, `bash`, `cpp`,
  `curl` or `wget`, and `sha256sum`.
- `bdftopcf` and `mkfontdir` (Debian/Ubuntu: `xfonts-utils`).
- The X11 development files for the host (Debian/Ubuntu: `libx11-dev
  libxt-dev libxext-dev`). Motif's build-time tools are compiled for the host
  first.
- **gcc14-cross-amix**, built and installed, with its AMIX sysroot. Follow its
  README; the sysroot comes from your own AMIX installation.
- **X11R6.3 for AMIX**:
  - its `xc/exports` tree (`include/` and `lib/` with the static libraries and
    the `.sa` stubs), copied from your AMIX machine into the sysroot, by
    default `$SYSROOT/usr/x11r6/xc/exports`;
  - its source tree (`xc/`): `libXp` is compiled from `xc/lib/Xp`, and
    `XKeysymDB`/`XErrorDB` come from `xc/lib/X11`.
- About 3 GB of disk space.

## Building

```sh
cp config.sh.example config.sh     # your paths
$EDITOR config.sh
./build.sh
```

`build.sh` runs these steps in order; each can also be run on its own:

| Step | What it does |
|---|---|
| `fetch.sh` | Downloads `cde-src-2.2.4a.tar.gz` from SourceForge and checks its SHA-256. |
| `scripts/build-libxp.sh` | A static `libXp` from the X11R6.3 tree. |
| `scripts/build-cde.sh` | Unpacks and patches CDE, prepares the build (see below), and builds the libraries, the programs and the fonts. |
| `scripts/package-cde.sh` | `work/cde-amix.tar`: the `/usr/dt` tree, laid out by CDE's own install databases. |

What `build-cde.sh` prepares:
- `imports/`, with links to the X11R6.3 and Motif headers and static libraries;
- `amix-cxxfix/`, C++-usable copies of AMIX's K&R system headers, generated
  from your sysroot by `scripts/amix-cxxfix.py`;
- `config/cf/amixpaths.def`, with the paths from `config.sh`;
- the tools CDE runs during its own build, compiled for the host by
  `scripts/hostbuild.sh`.

`build.sh` links X, Motif and libstdc++ statically, about 166 MB in all.
`scripts/build-shared-cde.sh STAGE` builds the same tree in `work-shared/`
against shared X and Motif, with `libDt*.so.2` in `/usr/dt/lib`: about 38 MB,
and what the system runs now. AMIX's `libc`, `libsocket` and `libnsl` are
shared in both. Logs are in the build directories:
`make-lib.log`, `make-programs.log` and the others in `work/cde-2.2.4a/`.

## Installing on AMIX

Copy `work/cde-amix.tar` to the machine, then as root:

```sh
cd / && tar xf /var/tmp/cde-amix.tar && sh /usr/dt/AMIX-INSTALL.sh
```

`AMIX-INSTALL.sh` does these things:
- sets owners and modes;
- creates `/etc/dt` and `/var/dt`;
- writes `/etc/dt/config/sys.dtprofile`, the session environment new users
  get: `PATH` with AMIX's tool directories, X11R6.3's shared libraries first
  (if installed), and bash in terminals (if installed);
- links X11R6.3's versioned shared libraries into `/usr/x11r6/lib/r63`;
- writes `/etc/dt/config/C/sys.resources`, which shows dotfiles in file
  dialogs;
- compiles the ToolTalk types on the machine;
- installs a `dtterm` terminfo entry.

Files that already exist in `/etc/dt` are left as they are. Users who already
have a `~/.dtprofile` keep it; add the `AMIX:` lines from the end of
`/etc/dt/config/sys.dtprofile` to it.

It does not edit `/etc/inetd.conf` or `/etc/services`. It prints the optional
network services (`dtspcd`, `rpc.ttdbserver`, `rpc.cmsd`) for you to add by
hand.

Start a session with `startcde`, or set up dtlogin (below). `startcde` runs
X with CDE's session. It uses `xhold` to keep the X server from resetting
between the session's first clients, which an X server without `-noreset`
otherwise does. `STARTX` chooses the program that starts X: `startx` by
default, for example `STARTX=/opt/amix/bin/startx-z3 startcde 1920x1080` on a
Z3660.

`target/` holds optional extras for the machine; the scripts in it expect
their files in `/var/tmp`:

| | |
|---|---|
| `target/dtlogin/` | dtlogin at boot through `/etc/init.d/dtlogin` and `dtconfig`. `/etc/dt/nodtlogin` skips it. Includes Motif, TWM and OPEN LOOK sessions next to CDE, and `Xzz9000-dt` as the server dtlogin starts. `mklogo.py` turns a picture of your choice into a dtlogin logo; none is included. |
| `target/keymap/` | The console's keyboard layout for X. `kmap2xmodmap.py` converts your machine's `/usr/amiga/lib/kmap/*` into xmodmap tables, and `xkeymap-amiga` loads the one that matches the console. |
| `target/extra/XLC_LOCALE.C.amix` | A C locale for X11R6.3 with both halves of ISO 8859-1, so dtterm shows umlauts and other 8-bit characters. |

## What builds and what does not

Built: 71 programs in `/usr/dt/bin` with their libraries, app defaults,
icons, message catalogues and fonts. In use on the A3000:
- dtlogin, dtsession and dtwm with the front panel;
- dtterm, the file manager and dtpad;
- the style manager, the help viewer and dtcalc.

Not built (`work/dtroot/MISSING` lists the files):
- **dtksh**: the ksh93 build needs a Korn shell on the host.
- **dtbuilder, dtcodegen, ttsnoop**: they need Motif's `libUil`.
- **dtinfo and dtinfogen**: their C++ (DtMmdb, nsgmls) does not build yet. The
  front panel's book icon therefore does nothing.
- **The help volumes** (`doc/C`): they need dtdocbook's tools on the host
  (nsgmls, instant) and a Korn shell. The help viewer runs, but most topics
  are missing.

## Known issues

- dtwm can report `Could not connect to ToolTalk: TT_ERR_AUTHORIZATION` right
  after login. ToolTalk works afterwards; it looks like a start-up race.
- The static build's binaries are large, about 5 MB each. The shared build
  does not have this.
- The patches favour working on AMIX over generality. Some areas are untested
  on the A3000: dtmail, dtcm and printing.

## What the port changes

The patches in `patches/cde/` are one commit each. They cover these areas:
- **The AMIX platform:** `amix.cf`, `host.def`, and stand-ins in
  `config/amix/include` for headers SVR4.0 lacks (`wchar.h`, `wctype.h`,
  `fnmatch.h`, `iconv.h`, prototypes for TLI, sockets and RPC).
- **`fork()` instead of `vfork()`.** AMIX's `vfork` shares memory, and CDE's
  children change the environment before they exec.
- **dtlogin:** reads `/etc/shadow`.
- **dtterm:** STREAMS ptys.
- **SVR4.0 differences**, for gcc 14, in ToolTalk, DtSvc, DtHelp and DtSearch.

## Licences

- Motif 2.3.8 is under the GNU LGPL 2.1, and CDE under the GNU LGPL 2.0 or
  later. The patches are changes to those works and carry the same licences.
- The scripts and the other files in this repository are under the GNU LGPL
  2.1 or later (`LICENSE`).
- AMIX is not free software. Nothing from it is in this repository: the
  sysroot, the C++ header copies (generated at build time), the keymaps
  (generated on your machine) and any logos stay on your side. Your build
  links AMIX's shared libraries at run time on your own machine.

## Credits

- The CDE project (cdesktopenv) and The Open Group, for CDE.
- ICS and The Open Group, for Motif.
- asokero, for gcc14-cross-amix and the AMIX 68040/68060 kernel port.
- jusii, for the Z3660 drivers and `Xzz9000`.
- The amigaux.org community.
