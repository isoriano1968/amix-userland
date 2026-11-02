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

A shared library needs these, after linking:

```make
../../../tools/amix-got-check build/*.o
m68k-elf-gcc -shared -Wl,-h,libfoo.so.1 build/*.o -o build/libfoo.so.1
../../../tools/amix-fix-data-relocs build/libfoo.so.1
```

Run `amix-got-check` on the objects: it finds a GCC 14 `-fPIC` fault that gives wrong
data with no error. Run `amix-fix-data-relocs` on any library that exports data.

`stage` installs `libfoo.so.1`, the link `libfoo.so`, the headers and `foo.pc`. The
`.pc` file carries no runpath: `tools/amix-pkg-config` adds one for cross builds, and
`LD_RUN_PATH` does it on the Amiga.

AMIX's own `ld` crashes on a library made by current binutils, so `libfoo.so` on the
Amiga must be a link stub. `desktop/cde/scripts/stage-stubs.sh` writes the assembler
for one with `tools/amix-link-stub`, and the Amiga's own `as` and `ld` build it.

### 6. Test it

Install into the sysroot and on the Amiga, then run it there:

```sh
make build sysroot-install
tar cf foo.tar -C build/stage usr
python3 ../../../tools/net/netsend.py foo.tar 5555
```

On the Amiga, as root:

```sh
cd /tmp
netget <your PC> 5555 foo.tar
cd /
tar xf /tmp/foo.tar
rm /tmp/foo.tar
```

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
