# GNU Pth for Amiga UNIX

This is a port of **GNU Pth 2.0.7** to Amiga UNIX (AMIX), built with an
AMIX-adapted **GCC 14.2** cross-compiler and **68060 optimizations**.

The aim is practical: let AMIX software use an established userspace threading
library and its pthread-compatible interface. The port works on the updated
physical AMIX machine used for testing, without a kernel change or loadable
module.

This repository provides the **build recipe, three small patches, and test
sources**. You fetch GNU Pth from upstream and build it using your own AMIX
sysroot. Compiled libraries, AMIX system files, credentials, and VM images are
not included.

## What kind of threads does this provide?

GNU Pth schedules threads inside one process. Its threads cooperate: another
thread runs when the current one yields, waits, or uses an appropriate wrapped
I/O operation. It also provides a pthread emulation library, `libpthread.a`,
so applications can use familiar calls such as `pthread_create()` and
`pthread_join()`.

This gives us a useful starting point for porting applications. It does not
provide preemptive kernel threads or make CPU-bound work run faster on a single
68060. Applications must be built with GNU Pth's headers to use its soft syscall
mappings; blocking calls inside arbitrary precompiled libraries can still block
the process. This port does not claim complete modern POSIX pthread conformance.

## What has been tested?

The library and acceptance programs were cross-built on Ubuntu 24.04 and run on
a physical 68060 AMIX machine reporting `68060-261027-ia`.

| Test | Result |
| --- | --- |
| GNU Pth's upstream standard and pthread tests | Passed |
| Producer/consumer application using GNU `libpthread.a` | 600 items, correct checksum, passed |
| Per-thread `errno`, cleanup handlers and thread-specific data | Passed the included tests |
| A pipe reader waiting while another thread makes progress | Passed using GNU's wrapped I/O |
| Timed condition wait | Returned the expected timeout |
| Once initialization that yields or exits | Passed after the AMIX fix |
| pigz 2.8 with two worker threads | Compressed 256 KiB; native gzip decompression reproduced the input exactly |

**pigz is the first practical application tested.** Python 3 is the next larger
porting target to investigate. Python 3 and newer Mesa/Gallium are not working
ports delivered by this repository.

See [the port notes](docs/GNU-PTH.md) for the exact test scope, fixes and remaining
limitations. The compiler runtime was built with threading disabled; installing
this library does not by itself enable C++ `std::thread` or compiler TLS.

## Build on Linux

You need GNU build tools, Python 3, an AMIX-adapted GCC 14 cross-toolchain, and
your own AMIX headers and libraries. A generic `m68k-elf-gcc` is insufficient
because AMIX needs ABI and linker/specs fixes.

The tested toolchain is [asokero/gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix)
at revision `58130fe7a322a19cb4c010a8257444cf1c016a07`.

From the repository directory:

```sh
export CC="$HOME/opt/m68k-amix/bin/m68k-elf-gcc"
export PTH_PREFIX="$HOME/opt/pth-amix"
bash tools/build-gnu-pth.sh
sh tools/build-proof.sh
```

The library build script downloads the pinned GNU Pth archive, verifies its
SHA-256 checksum, configures it, applies the AMIX fixes, and builds static
libraries. The second command builds the acceptance programs; run those on
AMIX, not on the Linux host.

Follow the [complete build recipe](docs/BUILD-RECIPE.md) for prerequisites,
source checksums, test instructions, and the pigz application build. Our local
[build environment notes](docs/BUILD-ENVIRONMENT.md) describe the Linux VM used
for development; that particular VM is not required to reproduce the port.

## Install on AMIX

The fixed installation prefix is **`/usr/local/gnu`**. Installation uses a simple
shell script, with no dependency on AMIX's old package manager.

After building and testing, create the installation archive on Linux:

```sh
python3 tools/stage-amix-install.py
```

Transfer `dist/gnu-pth-amix-install.tar.gz` to a fresh directory on AMIX, then
run there as root:

```sh
gzip -dc gnu-pth-amix-install.tar.gz | tar xf -
sh install.sh
```

The installer checks the staged files, refuses to replace differing existing
files, and compares each installed file with its source. Repeating an identical
installation is supported. The physical test installation verified all 15 files.

| Directory | Contents |
| --- | --- |
| `/usr/local/gnu/include` | `pth.h`, `pthread.h` |
| `/usr/local/gnu/lib` | `libpth.a`, `libpthread.a` |
| `/usr/local/gnu/bin` | `pth-config`, `pthread-config` |
| `/usr/local/gnu/man` | Library and config-helper manual pages |
| `/usr/local/gnu/share/doc/gnu-pth` | Licence, port notes, build receipt, file manifest and checksums |

For application build flags, use:

```sh
/usr/local/gnu/bin/pthread-config --all --cflags --ldflags --libs
```

The installer does not change global shell profiles or replace AMIX's libc.
The file manifest is `share/doc/gnu-pth/INSTALL-FILES` under the GNU prefix.

## The AMIX fixes

The [patches directory](patches/README.md) contains three reviewable changes:

1. Save and restore AMIX's process-global `errno` when switching contexts.
2. Expose the missing public `timespec` type consistently with GNU Pth's private fallback.
3. Prevent a yielding `pthread_once()` initializer from running more than once,
   and allow retry if the initializer exits or is cancelled.

The normal build applies the equivalent Python helper. For manual builds, the
patches apply after configure; the recipe explains the required build order.

## Repository layout and licensing

- `tools/`: build, staging and development helpers.
- `patches/`: GNU Pth port changes.
- `tests/` and `probes/`: focused acceptance tests and context probes.
- `ports/pigz/`: small compatibility additions for the pigz application test.
- `docs/`: build instructions, measured results and porting notes.
- `poc/`: the original experiments and shared application proof. The custom
  runtime here is historical; GNU builds never link `poc/pthread.c`.

GNU Pth and its port modifications retain GNU Pth's **LGPL 2.1-or-later** terms.
The original standalone proof and tests use the [MIT licence](poc/LICENSE).
