# Creating a package

How a piece of software becomes a component of amix-userland. A component is the
recipe; a package is what the recipe installs on the Amiga. The tree has the
recipes only.

## The rules

- **One component, one package.** A library that a program needs is a component
  of its own, never built inside the program that uses it. Motif, `libXp`, GLU
  and freeglut each came out of larger components for this reason.
- **Requirements are declared.** Every component has a manifest, and `make build`
  refuses to work from a wrong one.
- **Libraries are shared and complete.** A library installs its `.so.N`, its
  headers and a `.pc` file, so that someone can also develop on the Amiga itself.
  See "Libraries" below.
- **Programs find their libraries by themselves.** They carry a runpath. A launcher
  that sets `LD_LIBRARY_PATH` is a fault to fix.
- **Things go where they belong:** `/usr/local` for most software, `/usr/gnu` for
  GNU tools that clash with AMIX's own, `/usr/x11r6`, `/usr/motif` and `/usr/dt` for
  X, Motif and CDE, `/opt/<name>` for programs with their own data tree.
- **Fixes name their compiler.** A workaround for GCC 2.7.2.3 on the Amiga must not
  change the GCC 14 build, and the reverse: guard it, as with `__GNUC__ < 3`.
- **No binaries, nothing that is not ours to give.** No AMIX files, no game data,
  no modules, no built results.

## Where things go

| What | Where |
| ---- | ---- |
| Programs and libraries for AMIX | amix-userland, `components/` |
| Kernel, drivers, what runs on the board's ARM core | [amix-gate](https://github.com/isoriano1968/amix-gate) |

A package may need both: `x11/mesa-183` is here and takes `libGL` and `libzzgl` from
amix-gate's `firmware/core1`.

## Step by step

### 1. Make the directory

Pick the category from the table in the [README](../README.md). A new library goes
in `library`, a GNU tool in `gnu`.

```sh
cd components/library
mkdir foo
cp ../../templates/Makefile ../../templates/component.manifest foo/
cd foo
mv component.manifest foo.manifest
```

### 2. Fill in the Makefile

Set the five `COMPONENT_` variables. The archive is fetched from `COMPONENT_URL` and
checked against `COMPONENT_SHA256`, so never leave that out.

A component has these targets; `make-rules/shared.mk` provides those in italics:

| Target | Does |
| ------ | ---- |
| `build` | Fetches, unpacks, patches and compiles into the component's directory |
| `stage` | Puts what goes on AMIX in `build/stage`, at its AMIX paths |
| *`requires`* | Checks the manifest, and builds what is missing |
| *`sysroot-install`* | Runs `stage` and copies the result into the sysroot |
| *`clean`* | Removes what a build made |

Compile with `m68k-elf-gcc`, for shared libraries also with `-fPIC`, and take library
flags from pkg-config, which adds the right runpath:

```make
CFLAGS += `$(PKG_CONFIG) --cflags x11`
LIBS   += `$(PKG_CONFIG) --libs x11`
```

Three good models: `gnu/grep`, a program using the shared
recipe `components/gnu/gnu.mk`; `library/zlib`, a shared library built by a plain
compile loop; and `library/freeglut`, where a hand-written `files/config.h` replaces
the CMake step.

### 3. Write the manifest

```text
# name, version and what building and running it needs
name     library/foo
version  1.2.3
summary  What it is, in a line
require  library/zlib
provide  foo
```

`name` is the component's place under `components/`. A `require` is another
component. A `provide` is a pkg-config name the component installs.

**Take the requirements from the upstream build files,** `configure.ac`,
`CMakeLists.txt` or `meson.build`, then compare with `readelf -d` of what you built.
Do not guess from memory: every library in `NEEDED` must trace back to a `require`,
apart from AMIX's own.

Check it, from anywhere in the tree:

```sh
tools/amix-requires components/library/foo
```

It prints the build order, or says what is wrong. `make build` runs the same check
first: a `require` that is no component stops the build, and a library that is not
in the sysroot is built from its component and installed there. We have no
repository of built packages yet, so the source is the package; a `pkg` tool will
later fetch a binary when one exists.

### 4. Port it

Keep our changes apart from upstream's:

- `patches/` holds numbered changes to upstream files, applied at `build`.
- `files/` holds files that are ours.

What usually needs attention:

| Problem | What to do |
| ------- | ---------- |
| The 1991 libc lacks a function | A small replacement in the component; `gnu/compat/amix-gnu.c` for GNU tools |
| `snprintf`, `strcasecmp`, `usleep` | `libamixc99`: add `amixc99` to the requirements in pkg-config |
| `select`, `usleep`, `__iob` in a shared library | They are in the static part of libc, which a `.so` cannot use. Use `poll`, or let the program supply them |
| configure wants to run test programs | Answers in a cache file, or `ac_cv_` variables |
| Link order with shared libraries | `ld` reads a library named only by another `.so` after the archives: name it yourself, earlier |
| Assembler error `sub.l` with two memory operands | A GCC 14 bug: `-fno-split-wide-types` |

The 1991 `ld.so.1` runs no `.init_array` and passes over undefined symbols without a
word: a program can start and die at the first call. Read `readelf -d`, and read
`amix-got-check`'s output.

### 5. Libraries

Build the objects `-fPIC` into a static archive and let `tools/amix-mkshlib` make
the shared library; it runs every step below in order:

```make
../../../tools/amix-mkshlib build/libfoo.a libfoo.so.1 build -lbar
```

Use `-init` for a library with constructors: AMIX's `ld.so.1` calls `_init` but never
runs `.init_array`. What it does, if you need the steps by hand:

```make
../../../tools/amix-got-check build/*.o
m68k-elf-gcc -shared -Wl,-h,libfoo.so.1 build/*.o -o build/libfoo.so.1
../../../tools/amix-fix-data-relocs build/libfoo.so.1
```

`amix-got-check` finds a GCC 14 `-fPIC` fault that gives wrong data with no error;
`amix-fix-data-relocs` is needed on any library that exports data. The
[porting how-to](porting-howto.md) has the traps around exported symbols and
`_sys_siglist`.

`stage` installs `libfoo.so.1`, the link `libfoo.so`, the headers and `foo.pc`. The
`.pc` file carries no runpath: `tools/amix-pkg-config` adds one for cross builds, and
`LD_RUN_PATH` does it on the Amiga.

AMIX's own `ld` crashes on a library made by current binutils, so `libfoo.so` on the
Amiga must be a link stub: the exported symbols only, with the soname of the real
library, so the program runs with `libfoo.so.1`. With `make-rules/shlib.mk` this
comes by itself: `$(mkshlibs)` writes the stub's assembler (`tools/amix-link-stub`,
from the library and its `SHLIB_ARGS`), the stage puts it and a `LIST` into
`/usr/local/share/amix-stubs/<name>`, and `amix-install` builds the stub on the
Amiga with its own `as` and `ld`. A library whose export list is too long for
AMIX's `as` keeps only its API: `SHLIB_STUB_ONLY_libfoo.so.1 = ^foo_`.

### 6. Test it

Install into the sysroot and on the Amiga, then run it there:

```sh
make build stage sysroot-install
python3 ../../../tools/net/netsend.py build/foo-amix.tar 5555
```

On the Amiga, as root:

```sh
cd /tmp
netget <your PC> 5555 foo-amix.tar
amix-install /tmp/foo-amix.tar
rm /tmp/foo-amix.tar
```

`amix-install` comes from `sysutils/amix-pkgtools`; install that one first by
hand, `cd / && /usr/gnu/bin/tar --no-overwrite-dir -xf amix-pkgtools-amix.tar`.
`amix-stubs` (in the same package) builds the stubs again, for one package by name
or for all; run it when a library's `.so` link was replaced by hand.

Two rules keep a package from changing the system around it. The tar is made
with `--mode=go-w`, so nothing in it is group- or world-writable. It is
unpacked with GNU tar and `--no-overwrite-dir`, so directories that exist,
such as `/etc` or `/usr/local`, keep their owner and mode. AMIX's own `tar`
applies the archive's directory modes to them.

A library must also pass a native test: compile a small program on the Amiga with its
`gcc` through `pkg-config --cflags --libs foo`, and run it.

### 7. CDE entry

A desktop program gets an action, icons and an Application Manager file in
`/etc/dt/appconfig`, which a CDE rebuild leaves alone. `developer/nedit` is the
example. Log in again to see it.

### 8. A set of packages

A metapackage has a manifest with only `require` lines and a Makefile that builds them,
and no files of its own. `gnu/gnu-tools` is the example.

### 9. Finish

Add a `README.md` that says what it is, where it installs, and what is special about
AMIX, and `<name>.license` with the upstream licence. Add a `.gitignore` for `build/`,
the archive and the unpacked source. Put the name in the table in the
[README](../README.md).
