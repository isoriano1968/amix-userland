# Porting a component to AMIX: a how-to

For developers who want to bring a new library or program to AMIX. It ties
together the pieces the other documents describe one at a time, and walks
through building an existing component from start to finish.

- [getting-started.md](getting-started.md): the PC, the sysroot, the compiler,
  copying files to the Amiga.
- [creating-a-package.md](creating-a-package.md): the rules for a component and
  the traps of the 1991 C library and linker.
- This document: how a recipe works, which build system to use, and how a port
  goes from download to running on the Amiga.

## 1. The picture

```
upstream archive ──► recipe (components/<category>/<name>/) ──► make build
   (URL + SHA-256)        Makefile, manifest, patches/, files/        │
                                                                      ▼
            the Amiga ◄── tar of build/stage ◄── make stage ◄── build/ (objects, .so)
                                      │
                                      └──► make sysroot-install ──► the PC's sysroot,
                                                                     for the next component
```

A component is a **recipe**, not a copy of the software: where the source comes
from, its checksum, the changes AMIX needs, and how to build and install it. The
tree never holds the unpacked source or anything built.

## 2. The environment

You need what [getting-started.md](getting-started.md) sets up. In short:

| What | Where | Check |
| ---- | ----- | ----- |
| GCC 14 for userland | `~/opt/m68k-amix/bin` (`m68k-elf-gcc`) | `m68k-elf-gcc --version` |
| The sysroot (your AMIX's headers and libraries) | `~/opt/m68k-amix-sysroot` | `ls ~/opt/m68k-amix-sysroot/usr/lib/libc.so.1` |
| This tree | `~/Sources/usr-amix/amix-userland` | |
| Host tools | `make`, `patch`, `curl`, `python3`, `cmake`, `meson`, `ninja` (only for components that use them) | |

`make-rules/shared.mk` takes these defaults; override them on the `make` line if
yours differ:

| Variable | Default | Meaning |
| -------- | ------- | ------- |
| `AMIX_TOOLS` | `~/opt/m68k-amix/bin` | the cross compiler |
| `AMIX_SYSROOT` | `~/opt/m68k-amix-sysroot` | headers and libraries to build against |
| `PREFIX` | `/usr/local` | where the component installs on AMIX |
| `PKG_CONFIG` | `tools/amix-pkg-config` | pkg-config that looks only in the sysroot and adds the runpath |

The kernel and drivers are not built this way: they live in amix-gate and use
GCC 2.7.2.3. Never mix the two compilers.

## 3. What a recipe says: the package information

Every recipe starts with five variables. They are what a package tool needs to
fetch and verify the source, and `shared.mk` uses them for exactly that:

```make
# Expat 2.7.3 for AMIX: XML parser (libexpat.so.1, shared, with its headers
# and expat.pc); libdom's XML binding uses it, for SVG in NetSurf.
COMPONENT_NAME    = expat
COMPONENT_VERSION = 2.7.3
COMPONENT_ARCHIVE = expat-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://github.com/libexpat/libexpat/releases/download/R_2_7_3/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 71df8f40706a7bb0a80a5367079ea75d91da4f8c65c58ec59bcdfbf7decdab9f
```

- **The first lines** say what it is and what it installs. Every recipe starts
  this way, so `head -3 Makefile` tells you what a component is.
- **`COMPONENT_URL`** is upstream's own release location, never a mirror of
  ours. Prefer release archives over git snapshots.
- **`COMPONENT_SHA256`** is the archive's checksum. Get it once, from a download
  you trust, and compare it with what upstream publishes when they do:

  ```sh
  curl -fLO <the URL>
  sha256sum <the archive>
  ```

  `shared.mk` downloads to `<archive>.part`, checks the sum, and only then keeps
  the file. A wrong sum stops the build.

The **manifest**, `<name>.manifest`, says what the component needs and what it
offers:

```text
name     library/libupnp
version  22.1.10
summary  libupnp (pupnp): UPnP control point
provide  libupnp
require  library/pth
```

`require` names another component; `provide` names the pkg-config module this
one installs. `tools/amix-requires components/<category>/<name>` resolves the
requirements, prints the build order, and builds and installs into the sysroot
whatever is missing. `make build` runs it first.

There is no binary package repository yet: today the recipe is the package. A
`pkg` tool that fetches a built package when one exists, and falls back to
building, is planned on top of exactly this information.

## 4. Choosing the build style

Use the build system upstream ships. For the common ones `make-rules/` has a
rule set: the recipe sets the `COMPONENT_` variables and its options, includes
the rule set, and gets `make build` and `make stage` from it, shared library and
tar included. Copy the model component of the same style.

| Upstream builds with | Rule set | Model component | The recipe sets |
| -------------------- | -------- | --------------- | --------------- |
| Autotools (`configure`) | `configure.mk` | `library/expat` | `CONFIGURE_OPTIONS`, `CONFIGURE_CFLAGS`, `CONFIGURE_ENV`, `INSTALL_TARGETS` |
| CMake | `cmake.mk` | `library/libupnp` | `CMAKE_OPTIONS`, `CMAKE_CFLAGS` (the toolchain file `amix-toolchain.cmake` is used for you) |
| Meson | `meson.mk` | `library/zstd` | `MESON_OPTIONS`, `MESON_SOURCE`, `MESON_CFLAGS` (the cross file is written for you) |
| NetSurf's buildsystem | `netsurf.mk` | `library/libcss` | `NS_LIBS` |
| A plain Makefile, or none | `unpack.mk` + `shlib.mk` | `library/bzip2` | its own compile lines; `$(unpack)`, `$(mkshlibs)`, `$(stage_finish)` |
| GNU tools | `components/gnu/gnu.mk` | `gnu/sed`, `gnu/gzip` | `PROGS`, `EXTRA_CONF`, `COMPAT` |

All rule sets share two pieces:

- **`unpack.mk`**: `$(unpack)` unpacks the archive fresh and applies
  `patches/*.patch` in order (`S`, the tree's name, defaults to
  `<name>-<version>`).
- **`shlib.mk`**: name the shared libraries and where they come from, and the
  rule set makes them and stages them:

  ```make
  SHLIBS                      = libfoo.so.1
  SHLIB_ARCHIVE_libfoo.so.1   = build/lib/.libs/libfoo.a
  SHLIB_ARGS_libfoo.so.1      = -L$(AMIX_SYSROOT)/usr/local/lib -lbar
  SHLIB_INIT_libfoo.so.1      = -init        # only with constructors
  STAGE_REMOVE                = lib/*.la lib/*.a lib/cmake share   # under the prefix
  ```

  Steps the rule set cannot know, such as making an archive from particular
  objects, go in `define BUILD_POST ... endef`; they run after the build and
  before the shared libraries (see `library/libupnp`).

A whole recipe, `library/expat`, is no more than this:

```make
COMPONENT_NAME    = expat
COMPONENT_VERSION = 2.7.3
COMPONENT_ARCHIVE = expat-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://github.com/libexpat/libexpat/releases/download/R_2_7_3/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 71df8f40706a7bb0a80a5367079ea75d91da4f8c65c58ec59bcdfbf7decdab9f

include ../../../make-rules/shared.mk

CONFIGURE_CFLAGS  = -O2 -fPIC -fvisibility=default -Disnan=__builtin_isnan
CONFIGURE_OPTIONS = --without-docbook --without-examples --without-tests --without-xmlwf
INSTALL_TARGETS   = lib:install .:install-pkgconfigDATA
STAGE_REMOVE      = lib/*.la lib/*.a lib/cmake share

SHLIBS                      = libexpat.so.1
SHLIB_ARCHIVE_libexpat.so.1 = build/lib/.libs/libexpat.a

include ../../../make-rules/configure.mk
```

When expat and libupnp moved onto the rule sets, their staged files stayed
byte for byte the same.

What every style has in common:

- Compile with `m68k-elf-gcc -m68060`, define `-D__amix__ -DSVR4`, and undefine
  `__linux__`: upstream code often keys on it.
- Never let a build system look at the PC's own headers or libraries. If
  `configure` finds `/usr/include/...` on the host, the build is wrong even when
  it links.
- configure tests that need to run a program cannot run on the PC: give the
  answer with `ac_cv_...` variables.
- Upstream test suites can often run on the PC, against a host build of the
  library's dependencies (`library/libcss` was checked that way; section 9).

## 5. Porting: pristine source plus patches

The archive is unpacked fresh by every `make build`, and every change to
upstream's files is a numbered patch in `patches/`. Our own new files go in
`files/`. Never edit the unpacked tree as the place where a change lives.

The working loop:

```sh
cd components/library/foo
make build                       # unpacks foo-1.2/, applies patches/, builds
# change files in foo-1.2/ until it builds and works
tar xf foo-1.2.tar.gz -C /tmp    # a pristine copy to compare against
cd /tmp/foo-1.2 && for p in ~/Sources/usr-amix/amix-userland/components/library/foo/patches/*.patch; do patch -p1 < $p; done
cd /tmp && diff -ruN foo-1.2 ~/Sources/usr-amix/amix-userland/components/library/foo/foo-1.2 \
    > ~/Sources/usr-amix/amix-userland/components/library/foo/patches/0002-amix-what-it-does.patch
```

Then prove it: `make clean build` must give the same files as your working
tree. A patch has a header line saying what it changes and why; one patch per
reason.

For changes taken from upstream's newer code (a backport), name the upstream
commits in the patch header. `library/libcss/patches/0002` is an example: it
takes upstream's `gap` commits and leaves out what depends on a feature 0.9.2
does not have.

## 6. Libraries

The ground rules (details in [creating-a-package.md](creating-a-package.md)):
a library is **shared**, has its **headers** and a **`.pc` file**, sits in its
proper prefix, and programs find it through their **runpath**, never through
`LD_LIBRARY_PATH` in launchers. Static archives are not installed.

Most upstream build systems cannot make a shared library for this target. Build
the static archive with `-fPIC` and let `tools/amix-mkshlib` make the shared one:

```sh
tools/amix-mkshlib [-init] ARCHIVE SONAME OUTDIR [LINK-ARGS...]
# e.g.
../../../tools/amix-mkshlib build/lib/.libs/libexpat.a libexpat.so.1 build
```

It checks the objects for GCC 14's `-fPIC` fault (`amix-got-check`), links
`OUTDIR/SONAME` with the libraries in LINK-ARGS as `NEEDED`, strips it, fixes the
exported data relocations (`amix-fix-data-relocs`) and makes the `lib<name>.so`
link. Use `-init` when the library has constructors: AMIX's `ld.so.1` calls a
library's `_init` but never runs `.init_array` (`tools/so-init`).

Traps that cost us days:

| Symptom | Cause and fix |
| ------- | ------------- |
| The `.so` exports nothing (`readelf --dyn-syms`) | The static build hid its symbols: `-fvisibility=default` after configure's `hidden` (expat), `--disable-symbol-hiding` (curl), or build from the shared targets' objects (libupnp) |
| `ld.so: relocation error: symbol not found: _sys_siglist` at start | `libamixc99`'s string helpers need it and AMIX's `libc.so.1` has none: the program must carry it, `-Wl,-u,_sys_siglist` (web/netsurf) |
| `select`, `gettimeofday`, `usleep` undefined in a `.so` | They are in the static part of AMIX's libc; the program supplies them |
| A program dies at the first call into a library | `ld.so.1` passes over undefined symbols silently: check `readelf -d` and every `NEEDED` |
| A small program dies in `dlopen((char *)0, ...)`, in `dl_makelist` | `ld.so.1` reads the `NEEDED` names of a library's own dependencies from the string table of the object `dlopen` was given. A large program survives on garbage names; a small one faults. Do not `dlopen` the program; call the library directly |

For native development on the Amiga, `lib<name>.so` there must be a link stub that
AMIX's own `ld` can read. `make-rules/shlib.mk` writes its source with
`tools/amix-link-stub`, the package carries it in
`/usr/local/share/amix-stubs/<name>`, and `amix-install` builds it on the Amiga.

## 7. Installing on the Amiga

`make stage` puts everything at its AMIX paths in `build/stage`; most recipes
also pack it as `build/<name>-amix.tar`. Copy the tar over and install it with
`amix-install` (from `sysutils/amix-pkgtools`, installed once by hand with GNU tar):
it unpacks at `/` with GNU tar, which leaves existing directories alone, and builds
the link stubs the package brought:

```sh
# on the PC, with ssh to the Amiga:
scp -O build/foo-amix.tar root@<amiga>:/var/tmp/
ssh root@<amiga> 'amix-install /var/tmp/foo-amix.tar && rm /var/tmp/foo-amix.tar'
```

Without ssh, use `tools/net/netsend.py` and `netget` as in
[getting-started.md](getting-started.md). Before replacing a library a running
program uses, copy the new one next to it and `mv` it over the old one: the
running program keeps the old file.

Check the free space first (`df -k /`); keep at least 200 MB.

## 8. Testing

1. **On the PC, if upstream has tests:** build the dependencies for the host
   and run the suite with your patches (section 9).
2. **On the Amiga, the program itself.** For a library, a small program
   compiled **on the Amiga** with its own `gcc` and
   `pkg-config --cflags --libs foo` proves the development install.
3. **What it pulls in:** `readelf -d` on the PC; every `NEEDED` library must come
   from a `require` in the manifest or be AMIX's own.

## 9. A worked example: building `library/libcss`

libcss is NetSurf's CSS engine: a shared library with two AMIX patches, built by
NetSurf's buildsystem. It needs `library/libparserutils` and
`library/libwapcaplet`, and `developer/netsurf-buildsystem`.

```sh
cd ~/Sources/usr-amix/amix-userland/components/library/libcss
head -3 Makefile                  # what it is
cat libcss.manifest               # what it needs
../../../tools/amix-requires .    # build order; builds what the sysroot lacks
make build                        # fetch (checked), unpack, patch, compile, libcss.so.0
make stage                        # build/stage/usr/local/{include,lib}, build/libcss-amix.tar
make sysroot-install              # so components that need libcss find it
```

What `make build` did, step by step (`make-rules/netsurf.mk`):

1. fetched `libcss-0.9.2-src.tar.gz` and checked it against `COMPONENT_SHA256`;
2. removed and unpacked `libcss-0.9.2/` fresh;
3. applied `patches/0001-amix-css-custom-properties.patch` (CSS `var()`) and
   `patches/0002-upstream-gap-row-gap.patch` (`gap`, from upstream);
4. ran NetSurf's buildsystem with `m68k-elf-gcc`, the sysroot's pkg-config and
   the static, `-fPIC` library type;
5. made `build/libcss.so.0` with `amix-mkshlib`.

Check the result:

```sh
m68k-elf-readelf -d build/libcss.so.0 | grep -E 'NEEDED|SONAME|RUNPATH'
m68k-elf-readelf --dyn-syms build/libcss.so.0 | grep -c css_stylesheet_create
```

Its own test suite runs on the PC against host builds of its two dependencies:

```sh
W=/tmp/libcss-host; NSB=~/opt/m68k-amix-sysroot/usr/local/share/netsurf-buildsystem
mkdir -p $W/inst && cd $W
for l in libwapcaplet libparserutils; do
    tar xzf ~/Sources/usr-amix/amix-userland/components/library/$l/$l-*-src.tar.gz
    make -C $l-*/ NSSHARED=$NSB PREFIX=$W/inst COMPONENT_TYPE=lib-static install
done
cd ~/Sources/usr-amix/amix-userland/components/library/libcss/libcss-0.9.2
CFLAGS=-Wno-error PKG_CONFIG_PATH=$W/inst/lib/pkgconfig \
    make NSSHARED=$NSB PREFIX=$W/inst COMPONENT_TYPE=lib-static test
```

Then install `build/libcss-amix.tar` on the Amiga (section 7) and run NetSurf,
which uses it.

## 10. Before you commit

- `make clean build stage` works from the recipe alone.
- The manifest's `require` lines match `readelf -d`.
- No AMIX files, no built results, no game data, no personal data or local
  paths in anything that is published.
- The component's `README.md` says what it is, where it installs and what is
  special on AMIX; `<name>.license` holds the upstream licence.

## 11. Planned

- **More recipes on the rule sets.** expat, libupnp, zstd, bzip2 and libXp use
  them; most older recipes still write their steps out. Move them one at a time
  and compare the staged files before and after.
- **A `pkg` tool** that installs a built package when one exists and otherwise
  builds it from the recipe.
