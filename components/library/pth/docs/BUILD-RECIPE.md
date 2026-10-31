# GNU Pth 2.0.7 for AMIX: source build recipe

This publication contains the recipe, AMIX patches, and acceptance tests. It
does not redistribute GNU Pth's complete source, compiled libraries, a compiler,
or licensed AMIX headers/libraries. Builders fetch upstream source themselves
and use their own AMIX installation/sysroot.

## Prerequisites

- Linux with a C/C++ build environment, make, curl, tar/gzip, Python 3 and patch.
- An AMIX-adapted GCC 14.2 toolchain with the pointer-return, struct-return,
  alignment, 68060 libgcc, and ELF/specs fixes. A generic m68k-elf compiler is
  insufficient. Tested toolchain source:
  https://github.com/asokero/gcc14-cross-amix,
  revision 58130fe7a322a19cb4c010a8257444cf1c016a07.
- The external, privately supplied AMIX sysroot required by that toolchain.

The host recipe was tested on x86-64 Ubuntu 24.04. Hardware acceptance used a
physical 68060 AMIX machine reporting 68060-261027-ia. Other hosts/platforms are
not implied to have passed those tests.

## Build GNU Pth

From this recipe directory:

```sh
export CC="$HOME/opt/m68k-amix/bin/m68k-elf-gcc"
export PTH_PREFIX="$HOME/opt/pth-amix"
bash tools/build-gnu-pth.sh
sh tools/build-proof.sh
```

The script downloads https://ftp.gnu.org/gnu/pth/pth-2.0.7.tar.gz and checks:

```text
72353660c5a2caafd601b20e12e75d865fd88f6cf1a088b306a3963f0bc77232
```

Build flags: -O2 -m68060 -std=gnu89 -fno-strict-aliasing. Configure enables
pthread emulation and static libraries with the mcsc/sc/mc context backend.
The source directory can be set with PTH_SOURCE. Source and build outputs stay
outside version control. No system headers or libc are replaced.

## Port patches

The automated recipe uses tools/patch-gnu-pth.py. Equivalent unified diffs are
provided in patches/ for review or manual application after configure:

1. 0001-amix-global-errno.patch: preserve process-global errno per context.
2. 0002-amix-public-timespec.patch: expose the missing public type, matching
   GNU Pth's existing private fallback. This patches generated pthread.h.
3. 0003-amix-yielding-once.patch: prevent duplicate initialization when the
   initializer yields; reset the once control on initializer exit/cancellation.

Do not apply both the helper and the diffs to the same fresh tree. Clean-build
after changing context macros: the old generated-header dependencies are
incomplete. The recipe orders pth_p.h generation before parallel compilation.

GNU Pth and the port modifications retain GNU Pth's LGPL 2.1-or-later terms.
Original standalone tests use the MIT licence in poc/LICENSE.

## Acceptance

Run the cross-built executables on AMIX, not on the Linux build host. Use
probes/bounded-run.c to bound external application tests. Tests include the
GNU upstream test_std/test_pthread, a producer/consumer pthread proof linked
only to GNU libpthread.a, errno isolation, wrapped pipe I/O with peer progress,
timed conditions, and once exit/nesting regressions. Check generated binaries
with the GCC toolchain's check-68060-safe.py before deployment.

The application test uses pigz 2.8 and zlib 1.3.1, fetched separately. Its build
script is tools/build-pigz-gnu.sh; it expects upstream working trees under
~/src/upstream. It disables Zopfli and uses the small private pigz port headers.
pigz -p 2 -b 32 compressed a 262144-byte input on AMIX; native gzip decompression
and cmp both exited 0. See docs/GNU-PTH.md for exact scope and limitations.

## Simple installation under /usr/local/gnu

On the Linux build host, after acceptance:

```sh
python3 tools/stage-amix-install.py
```

Transfer dist/gnu-pth-amix-install.tar.gz to a fresh AMIX scratch directory,
expand it, and run as root:

```sh
gzip -dc gnu-pth-amix-install.tar.gz | tar xf -
sh install.sh
```

The installer uses ordinary shell/file tools; no AMIX package-management tools
are required. It accepts identical existing files and refuses to overwrite
differing ones. All staged files are checked before copying, and each installed
file is compared byte-for-byte afterwards. The fixed prefix is /usr/local/gnu.

| Directory | Files |
|---|---|
| /usr/local/gnu/include | pth.h, pthread.h |
| /usr/local/gnu/lib | libpth.a, libpthread.a |
| /usr/local/gnu/bin | pth-config, pthread-config, reporting /usr/local/gnu |
| /usr/local/gnu/man/man1 and man3 | GNU config/library manual pages |
| /usr/local/gnu/share/doc/gnu-pth | licence, port notes, build receipt, manifest and checksums |

The install manifest is /usr/local/gnu/share/doc/gnu-pth/INSTALL-FILES.
Add /usr/local/gnu/bin to the desired developer shell's PATH, or invoke the
config helpers by absolute path. For static dependency flags, use
pthread-config --all --cflags --ldflags --libs. No global shell profiles are edited.
For removal, remove only the files listed in that manifest, preserving any
locally modified files. Do not remove the whole GNU prefix, which can contain
other GNU software.

## Limits

This is GNU Pth's cooperative pthread emulation, not Linux NPTL or full modern
POSIX conformance. Soft syscall mappings require GNU's headers at compile time;
arbitrary precompiled libc I/O is not intercepted. C++ std::thread/compiler TLS
are not enabled by this library installation. The once fix currently yields
waiting peers, so a long-blocking initializer can consume waiting CPU time.
