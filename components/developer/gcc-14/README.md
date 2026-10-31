# gcc14-cross-amix — a modern C and C++ cross compiler for Amiga UNIX

AMIX, Commodore's System V Release 4 for the Amiga, shipped in 1991. The best
compiler generally available for it is GCC 2.7.2.3, which has no C++ front end
installed, cannot be told about a 68060, and rejects C99. That rules out
building most software written since.

This builds a **GCC 14 cross compiler that targets AMIX**, with working C++
including exceptions and RTTI, and supplies the pieces of a C library that
SVR4.0 predates.

Verified on real hardware — an Amiga 3000 with a Mercury 68060, running AMIX
2.1c on a community-ported 68040/68060 kernel (stock AMIX runs on the 68030;
that kernel is a separate project). The compiler's output also runs on
68020/030/040, so a stock A3000UX is not excluded:

```
1 templates:  maximum(3,7)=7 maximum(2.5,1.5)=2.5
2 virtual:    area=36 (want 36)
3 new/delete: area=81 (want 81)
4 throw/catch: caught, why="unwound from four frames down"
all four ok
```

## What this is for

**Application software** — programs that run on top of AMIX and reach it
through libc and the ordinary system libraries. Three things make that
possible:

- **C++.** Templates, virtual dispatch, RTTI, `new`/`delete`, exceptions and
  the standard containers all work, so a C++ codebase builds without being
  rewritten. Porting a game, an emulator or any large modern application is the
  case this exists for.
- **C written after 1990.** C99 and later — declarations inside `for`, `long
  long`, `stdint.h`, `snprintf`.
- **68060 code that does not trap.** That CPU dropped the 64-bit multiply and
  divide; a binary containing them dies on it unless the kernel emulates them.
  This compiler does not emit them, and `tools/check-68060-safe.py` verifies
  any binary. Run it on every build, not once: one `%` on 64-bit operands
  brings the problem back.

One measurement, for scale. The AMIX Quake port timed `timedemo demo1` on the
real machine — 969 frames, 320x200, nothing else running:

| Build | fps |
|---|--:|
| GCC 2.7.2.3, `-O -m68040` | 7.5 |
| GCC 14.2.0, `-O1 -m68060` | **8.7** |

**1.16x from the compiler alone**, on the same source and the same hardware.
Worth keeping in proportion: the same benchmark found 3.6x from building for
the right CPU at all, and that port cannot use `-O2` because its renderer
breaks there. A newer compiler is a modest speedup; what it mainly buys is
being able to build the program in the first place.

**Not for the kernel, for device drivers, or for anything that has to be
object-compatible with what AMIX itself was built with.** This targets
`m68k-elf` rather than the system's own `m68k-cbm-sysv4`, and that kind of work
depends on era-correct linking. A period cross toolchain — GCC 2.7.2.3 with
binutils 2.8.1, targeting `m68k-cbm-sysv4` — exists as a separate project and
is the right tool there.

Two things to do before starting:

- **Repair the vendor's 1991 static libraries.** Every object in `libX11.a` and
  its siblings has a malformed `sh_link` field, and this linker will not
  interpret it — silently. The relocations are dropped, pointers stay zero, and
  the program jumps to address 0 the first time it uses one.
  `tools/fix-x-libraries.sh` writes repaired copies and leaves the originals
  untouched. Run it before linking anything that uses X.
- **Check the size of a hot loop.** GCC 14 optimises for speed over size by
  default, and the 68060 has an 8 KB instruction cache. `-Os` is worth
  measuring where it matters.

## How it works, in one paragraph

GCC dropped every m68k SVR4 target years ago, so this configures the generic
`m68k-elf` one — which is fine, because measured against the machine's own
binaries the plain m68k ELF ABI *is* the AMIX ABI. What AMIX-specific knowledge
exists lives in a specs file and a small compat layer rather than in a patch to
GCC. The packaged `m68k-linux-gnu` compiler cannot be used instead: its
`libgcc_eh.a` finds exception tables by asking a dynamic loader
(`_dl_find_object`) and locks with pthreads, and AMIX has neither. An `elf`
target builds the registry unwinder, where `crtbegin` registers `.eh_frame`
from `.init` at startup and no loader cooperation is needed.

[`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md) records every measurement behind those
choices. It is worth reading before changing anything here: several flags are
load-bearing in ways their names do not suggest.

## The 68060 trap, which is not only ours

libgcc's 64-bit divide, compiled for a 68020, uses `divul`, `divull` and
`mulul` — the 32x64 divide and 64-bit multiply forms **the 68060 removed**.
They are meant to be emulated by Motorola's integer support package in the
kernel; without one installed, they trap fatally. The symptom is that any
64-bit division *by a variable* kills the process, while dividing by a constant
is fine — because GCC folds that into a multiply and never calls libgcc at all.

This is why the toolchain defaults to `--with-cpu=68060`. It has to be the
default: `CFLAGS_FOR_TARGET` does not reach libgcc's own compile. Code built
this way still runs on 68020/030/040, because it only ever avoids instructions.

Anyone building m68k software for a 68060 without an ISP handler has the same
problem, whatever their compiler.

## What you need

- A Linux host with `gcc`, `g++`, `make`, `python3`, `curl` or `wget`, `bzip2`,
  `xz`, `m4`, `patch` and `file`, about 5 GB of scratch space, and network
  access to ftp.gnu.org and gcc.gnu.org. The build fetches GCC, binutils and
  GCC's own prerequisites (gmp, mpfr, mpc) itself.
- An **AMIX sysroot**, copied out of a licensed AMIX installation. None of it
  is in this repository and none of it may be committed here. What it must
  contain, mirroring the layout on the machine itself:

  | | |
  |---|---|
  | `usr/include` | the system headers |
  | `usr/lib` | `libc.so.1` and the other shared libraries |
  | `usr/ccs/lib` | `crt1.o`, `crti.o`, `crtn.o` and `libc.so` |

  Mount or copy an AMIX filesystem and take those three trees across. X11 is
  not needed for C or C++ on its own; `tools/sync-sysroot.sh` adds it when you
  come to link X clients. Other AMIX cross-toolchain projects use this same
  layout, so a sysroot assembled for one of those works here unchanged.

```sh
cp config.sh.example config.sh        # paths, CPU, this host's address
$EDITOR config.sh
toolchain/build-cross.sh              # once, about 25 minutes
```

`build-cross.sh` refuses to start if `$AMIX_SYSROOT/usr/include` is not there,
so the sysroot has to exist first. It builds binutils and GCC, installs the
specs and the repaired headers, builds the compat layer, and then libgcc and
libstdc++.

Two more steps, only if you are going to link X clients:

```sh
tools/sync-sysroot.sh /mnt/amix       # X11 headers and libraries into the sysroot
tools/fix-x-libraries.sh              # repair sh_link; writes copies, keeps originals
```

Then:

```sh
tools/amix-cc.sh -o probe/hello -O2 probe/hello.c
tools/amix-cc.sh -o probe/cxxprobe -O2 probe/cxxprobe.cpp
```

## Testing on a real machine

`tools/push-run.sh` serves a binary over TFTP, has the machine pull it into
`/root/amix-probe` and runs it, so nothing else on the target is written to.
The address and login come from `local/secrets.env`, which is gitignored;
`local/secrets.env.example` is the template.

This logs in as root over telnet, in clear text, because that is what a 1991
SVR4 offers. It is meant for a machine on a network you control.

`probe/` is the test battery, and it is how every fact in `docs/TOOLCHAIN.md`
was established. Each program asks one question and prints the answer, flushed,
in an order where a crash names the step it died in. Two of them
(`probe/compat.c`, `probe/stl.cpp`) also run natively via
`tools/test-compat-host.sh` — not redundancy: the formatter's battery passed on
the host and died on the Amiga, and that difference is what localised the CPU
fault.

## Status

Working: C and C++ compilation and linking, `libgcc`, `libsupc++`, a **hosted**
`libstdc++` with the standard containers, exceptions, RTTI, and the compat
layer (`snprintf`, `vsnprintf`, `usleep`, `stdint.h`, C++ linkage wrappers for
AMIX's C headers). `probe/stl.cpp` is the acceptance test and it passes.

Known and unfixed: `fprintf` reaches AMIX's own formatter, which predates C99
and mis-handles `ll` conversions -- the compat layer replaces `snprintf` and
`vsnprintf` but not `fprintf`. Anything printing a 64-bit value through
`fprintf` will print garbage. See `docs/TOOLCHAIN.md`.

## Licence

**MIT** — see [`LICENSE`](LICENSE).

Permissive rather than copyleft for one concrete reason: `compat/` is a
*runtime* library. `libamixc99.a` and `amix_init.o` are linked into every
binary this toolchain produces, so a copyleft licence here would reach into
other people's ports of other people's software. GCC solves the same problem
with a separate runtime-library exception; MIT is the simpler way to the same
place.

GCC and binutils are downloaded from ftp.gnu.org at build time and carry their
own licences; they are not redistributed here. Nothing from AMIX is
redistributed here either — see **What you need**.

## In amix-userland

This is Antti Sokero's [gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix)
(MIT, `gcc-14.license`) with our fixes: no fold-mem-offsets pass, pointers
returned in %a0 and %d0 as on SVR4, no inline FSIN/FCOS on the 68040 and 68060.
For userland only, never for the kernel or drivers: those are `developer/gcc-27`.
