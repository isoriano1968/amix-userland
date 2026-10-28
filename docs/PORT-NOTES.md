# Experimental Python 3.11 for AMIX

CPython 3.11.18 has been cross-built with the AMIX-adapted GCC 14.2 compiler
and run on the physical 68060 machine. Its threading backend links GNU Pth's
`libpthread.a`. The custom historical pthread runtime is not linked.

This milestone provides a scripting and threading proof staged under
`/tmp/pthreads-amix-261028/python/py311`. It does not install or replace a
system interpreter. The tested kernel reports `68060-261027-ia`.

## Physical results, 2026-10-28

Both the initial core interpreter and the later clean build passed:

- Arbitrary-size integer arithmetic and Python's ties-to-even float rounding.
- Unicode strings, JSON round-trips, and UTF-8 file contents.
- Four Python threads using a bounded queue, with 160 distinct work items.
- Thread-local values, thread joins, a timed Event wait, and a reentrant lock.
- Monotonic timing and conversion of Unix epoch zero to a calendar date.

The clean build also passed tests of zlib, gzip and ZIP round-trips on a
262144-byte input; known MD5, SHA-1, SHA-256 and SHA3-256 vectors; binary
struct packing; datetime; selected math edge cases; pipe/select I/O; and
environment updates. These are focused acceptance tests, not the complete
CPython regression suite.

The executable reported:

```text
3.11.18 (main, Oct 28 2026, 13:58:58) [GCC 14.2.0]
PYTHON3_AMIX_SMOKE_PASS
PYTHON3_AMIX_STDLIB_PASS
```

SHA-256 of the tested stripped clean-build executable:

```text
7fb086a0cdbefa0a3383dcb2b430553bd43adfa5a78eeb8eb1bfa92c3446a109
```

The toolchain's encoding-based instruction check inspected 1005 long
multiply/divide instructions and found no forms removed by the 68060.
The link map records the GNU `libpthread.a` archive. A second build from
the standalone repository in a fresh build directory also passed both
physical Python test scripts; its stripped executable is the hash above.

## Random-device prerequisite

Normal interpreter startup currently fails when obtaining its random hash
seed. The physical `/dev/urandom` node exists, but a native `dd` open returns
`No such device`. The Python tests above explicitly set `PYTHONHASHSEED=0`.
This is a controlled proof setting; it is not made the interpreter's default.
`os.urandom()` remains unavailable until the device is functional.

The user's driver source and object exist at `/usr/sys/amiga/driver/random.c`
and `random.o`, and the driver Makefile includes that object. The source
`/usr/sys/master.d/kernel.c` registers its entry points at major 48, matching
the device nodes. The older `/unix` contains `randopen`; the newer
`/stand/unix.z3` has no `randopen` or `randread` symbol. The actual selected
boot image still needs confirmation before any kernel change.
The source describes a compatibility PRNG without modern entropy accounting.
Enabling the interface and establishing cryptographic suitability are separate
tasks. The owner deferred kernel integration and will build the kernel
separately after the Python port. No boot or kernel files were changed for
this proof.

## Reproduce the cross-build

First build GNU Pth as described in [the GNU Pth recipe](https://github.com/isoriano1968/gnu-pth-amix/blob/main/docs/BUILD-RECIPE.md).
Use the same pinned, AMIX-adapted GCC 14 toolchain and your own AMIX sysroot.

From the project directory on Linux:

```sh
export CC="$HOME/opt/m68k-amix/bin/m68k-elf-gcc"
export PTH_PREFIX="$HOME/opt/pth-amix"
PYTHON_EXTRA_MODULES=yes bash tools/build-python-amix.sh
python3 tools/stage-python-proof.py
```

The script fetches the official CPython archive and verifies its SHA-256:

```text
9120e0a653868f6e3698d36cdd99e5a58903ec9469e448f21d98f7b16d8fa290
```

Source: [Python 3.11.18](https://www.python.org/downloads/release/python-31118/).

A matching Linux-native interpreter is built only to generate files needed
for cross-compilation. Every AMIX target object uses GCC 14.2 with `-O2
-m68060 -fno-strict-aliasing`; CPython uses GNU C11 mode and 1 MiB default
worker stacks. A clean rebuild can be requested with `PYTHON_CLEAN=yes`.
`PYTHON_SKIP_CONFIGURE=yes` is only for incremental development with an
already configured build tree.

The extra-module build expects the previously cross-built zlib 1.3.1 tree at
`~/src/upstream/zlib-1.3.1`; override `ZLIB_SOURCE` if needed. Other paths can be
set through `PYTHON_SOURCE`, `PYTHON_BUILD` and `BUILD_PYTHON`.

To build that dependency from a separately downloaded upstream zlib 1.3.1
source tree, use the same cross-toolchain:

```sh
(
    cd "$HOME/src/upstream/zlib-1.3.1"
    CC="$CC" AR="$(dirname "$CC")/m68k-elf-ar" \
        RANLIB="$(dirname "$CC")/m68k-elf-ranlib" \
        CFLAGS='-O2 -m68060 -std=gnu89 -fno-strict-aliasing' \
        ./configure --static
    make -j4 libz.a
)
```

Output is `~/build/python311-amix/python`. Keep the unstripped executable for
debugging and strip a copy for transfer. Run the 68060 instruction checker on
that copy. `dist/python311-amix-stdlib.tar.gz` contains the pure Python library
and smoke test, with GUI/test/ensurepip/venv trees omitted. Extract it into
the scratch directory to create `py311/`, and place the executable at
`py311/bin/python3.11`.

For this machine's current random-device limitation, run from the scratch
directory:

```sh
PYTHONHASHSEED=0 PYTHONHOME="$PWD/py311" py311/bin/python3.11 -S py311/smoke.py
```

The separate test `tests/python3-stdlib.py` checks the extra modules. Bound
hardware experiments with the cross-built `probes/bounded-run.c` launcher.

## Port changes

`tools/patch-python-amix.py` applies the source changes; the equivalent diff
is [ports/python/cpython-3.11-amix.patch](../ports/python/cpython-3.11-amix.patch).
Apply one approach to a fresh tree, not both.

- Recognize AMIX in the two cross-platform configure checks.
- Use elapsed `times()` ticks for monotonic time. The measured resolution is
  1/60 second; the adapter tracks 32-bit counter wrap between observations.
- Provide a bounded tty-name buffer without the unavailable `_SC_TTY_NAME_MAX`.
- Use zlib's actual `uInt` type for its output-count pointers. On this GCC
  target, `uint32_t` is unsigned long while zlib's `uInt` is unsigned int.

Private adapters in `ports/python/` supply missing wide-string operations,
math functions, environment updates, time-result copies, and declarations for
existing libc calls. They are linked into Python and never installed over
vendor headers or libraries. Python uses its own UTF-8 locale conversion
path; AMIX libc's locale is unchanged.

The time/environment adapters rely on cooperative execution: no scheduling
point occurs while copying static libc results or updating the environment.
They have not been validated with preemptive kernel threads.

## Current limits

GNU Pth remains cooperative. CPU-bound Python threads need explicit yielding
or suitable waiting/I/O operations; this does not provide parallel CPU work.
Signal handling, fork with live threads, extension loading and the complete
cancellation/timeout surface have not passed a full audit.

The proof includes static math, struct, binascii, JSON, random, datetime,
select, builtin hash and zlib modules. Socket, SSL, ctypes, SQLite and
subprocess extension modules have not been built for this milestone. There
is no pip/bootstrap or package-compatibility claim yet.

The configured `off_t` and `time_t` types are 32 bits. Large-file and
post-2038 behavior are outside this acceptance milestone.

The private math fallbacks passed focused hardware checks. A Linux comparison
sampled 4000 inputs each for expm1/log1p/log2/exp2 with a maximum observed
difference of one ulp, plus bit-exact nextafter cases. This is not a complete
libm accuracy or floating-exception conformance result.

Raw development evidence is in ignored `local/logs/python-linux.log` and
`python-amix.log`; compiler commands are displayed in visible terminals.
Kernel sources, licensed sysroot files and generated binaries remain outside
Git. CPython-derived changes use the PSF licence in `ports/python/LICENSE.PSF`;
original adapters/tests use the project's MIT licence in `LICENSE`.
