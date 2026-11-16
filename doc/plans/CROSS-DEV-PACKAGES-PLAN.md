# X11R6.3, Motif and CDE as packages for cross development

Plan of 2026-11-14.

## Goal

The CDE sample app (and any Motif/CDE program) builds on a cross host from
packages alone:

    pkg install amix-cross-dev-desktop
    m68k-elf-gcc ... `m68k-elf-pkg-config --cflags --libs xm dt`

and the same component builds also give the AMIX box packages (runtime,
with mwm, plus the development files under `facet.devel`). Nothing is copied
into a sysroot by hand; `make sysroot-install` into a packaged sysroot is
refused (`tools/amix-sysroot-check`).

## Where the three components stand

| Component | Sources | Shared libraries | Manifest |
|---|---|---|---|
| x11/x11r6.3 | pristine xc-1/2/3.tar.gz + `patches/0001-amix-port.patch` (`install.sh`, `SOURCES.sha256`) | `files/build-shared.sh`, run by hand in a tree outside the repository (`private/shared-build`) | no file list |
| desktop/motif | motif-2.3.8.tar.gz + patches | `scripts/build-shared.sh` into the same private stage | no file list |
| desktop/cde | cde-2.2.4a + patches | the same private stage | no file list |

The X11R6.3 libraries also need, today:

- the imake cross configuration (`x11/cross/host.def`, `imakecpp.sh`) from a
  clone of amix-z3660rtg at no fixed commit;
- `/usr/amiga`, `libscreen.so` and `libdbm.a` read with `ufsget.py` from an
  AMIX disk image (`x11/xserver-amix/build.sh` steps 2-4);
- a sysroot that already holds an older `/usr/x11r6`, so a build can pick up
  old headers or libraries without notice.

## Step 1: proper amix-userland components

For each of the three, in this order (each depends on the one before):

1. **Inputs pinned**: every download with its sha256 (`SOURCES.sha256`), and
   amix-z3660rtg's cross files at a fixed commit with its sha256. The AMIX
   files (`/usr/amiga`, `libscreen.so`, `libdbm.a`, `usr/ucblib`) come from
   the sysroot, which is amigaux/amix-system: no disk image any more.
2. **Clean sysroot only**: the build runs against the base sysroot plus the
   packages it declares (`require`), never a sysroot that already has its
   own output in it. On the P52 that is a scratch sysroot made from
   amix-system exactly as developer/amix/sysroot makes it.
3. **One make target** does what is now done by hand: unpack, patch, imake
   cross configuration, the shared libraries (`build-shared.sh`), the stage.
   No `private/` tree.
4. **Manifest with every file**, versions on every `require`, and the
   development files marked for `facet.devel`.
5. **Check**: the libraries from the new build are compared with the ones
   in use on the box (symbols, sonames, NEEDED, sizes); differences are
   explained before anything replaces them.

x11r6.3 also keeps the `Xamix` server in x11/xserver-amix, which uses the same
tree with the server patch series; only the client libraries, headers,
`.pc` files, `libXtInherit.a` and `xauth` belong to x11/x11r6.3.

## Step 2: cross packages on OpenIndiana

oi-userland components in the OpenIndiana project, built on the OI VM with
the packaged gcc-14 against the packaged sysroot, from the amix-userland
component at a pinned commit:

| Package | Contents (under /opt/m68k-amix-sysroot) | Requires |
|---|---|---|
| developer/amix/x11r6.3 | usr/x11r6: headers, lib*.so.7 + links, .pc | developer/amix/sysroot |
| developer/amix/motif | usr/motif: Xm/Mrm headers, libXm/libMrm, .pc, uil | developer/amix/x11r6.3 |
| developer/amix/cde | usr/dt: Dt/Tt/csa headers, libDt*/libtt/libcsa, .pc | developer/amix/motif, x11/libxp's part |
| metapackages/amix-cross-dev-desktop | the three + amix-cross-dev | versioned |

All `facet.devel`; the m68k files bypass pkgdepend as in the sysroot package.

## Order and tests

1. x11r6.3 component (P52), then its OI package; test: xauth and a small
   Xt client link on the Mac; on the box only later, with the box free.
2. motif, then cde, the same way.
3. amix-cross-dev-desktop; test: the CDE sample app built on the Mac.
4. Box side: the same components as IPS packages for the AMIX client
   (IPS-INSTALLER-PLAN.md), runtime and development split by facet.

The toolchain switch to the amigaux line (68020-60 default, SVR4 struct
return) comes after this and rebuilds all three with everything else.

## x11r6.3: analysis before the first build (2026-11-14)

- **Sources reproduce.** `install.sh` (xc-1/2/3.tar.gz, SOURCES.sha256,
  0001-amix-port.patch) gives a tree that equals the one the box's
  libX*.so.7 were built from (`private/shared-build/xc`), except for what
  x11/xserver-amix/build.sh changes at build time (HasShm YES, site.def includes
  host.def), the server patch series, and whitespace in three Imakefiles.
  The older copy `usr-amix/usr/x11r6/xc` lacks two fixes the patch has
  (AmixHasShm switch, two-argument gettimeofday for `__amix__` in Xos.h) and
  holds nine `.orig` leftovers: it is not an input any more.
- **Cross configuration.** `x11/cross/host.def` and `imakecpp.sh` are
  unchanged since jusii/amix-z3660rtg 91667de (its public HEAD): that commit
  is the pin, as a downloaded archive with its sha256.
- **No disk image, no mirror sysroot.** Everything build.sh read from the
  HDF is in amigaux/amix-system and resolves inside the packaged sysroot:
  usr/lib/libscreen.so -> ../amiga/lib/libscreen.so, usr/ccs/lib/libdbm.a
  -> ../../ucblib/libdbm.a, usr/include/amiga, and the libc.a members of the
  server's stub archive (select.o, syslog.o, fpsetmask.o, ...). The stub
  archive is a server matter (ServerExtraSysLibs) and stays in xserver-amix.
- **MIT-SHM** comes from `#define AmixHasShm YES` in site.def (the patch's
  switch), not from sed over amix.cf.
- **Toolchains.** imake generates Makefiles for gcc-27's names
  (m68k-cbm-sysv4-*), build-shared.sh then compiles every object with gcc-14
  (CC=m68k-elf-gcc, -O2 -m68060 -fPIC) and links with gcc -shared; it also
  needs amix-userland's tools/amix-got-check, amix-fix-data-relocs,
  amix-fix-needed. So the build requires both toolchains plus host gcc, cpp
  and GNU make. build-shared.sh has $HOME/opt/m68k-amix/bin hard-coded and
  must take the toolchain path from the make rules; gcc-14 gets
  --sysroot=$(AMIX_SYSROOT) so it never sees the P52's hand-made sysroot
  with its old /usr/x11r6. imakecpp.sh calls /usr/bin/cpp, which OI does
  not have under that name: it must use the host compiler's cpp.
- **-m68060** is hard-coded in build-shared.sh: it becomes the AMIX_CPU
  variable, so the later switch to the 68020-60 default is one setting.

## x11r6.3: done in amix-userland (2026-11-14)

One `gmake AMIX_SYSROOT=... stage` from the pinned inputs, 46 s on the P52,
against a clean sysroot made from amigaux/amix-system
(~/opt/m68k-amix-sysroot-base, kind=local). Result: nine of ten libX*.so.7
byte-identical to the libraries in use, libXext different only in object
order (same symbols); headers (287) and .pc files identical; xauth built.
Two manual steps of the old build are now in the build: makestrs as a host
program (Xt's generated headers), and the .pc files' Requires/extra libraries
plus stripping (they had been edited by hand). The manifest lists all 331
files, 320 of them marked devel. amix-requires now reads
`require X >= VERSION` and `file PATH devel`.

It requires library/amixc99 (x11.pc), which amix-requires built into the
base sysroot: on OpenIndiana that means a developer/amix/amixc99 package
before developer/amix/x11r6.3.
