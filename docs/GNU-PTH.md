# GNU Pth pthread emulation for AMIX

GNU Pth 2.0.7 is the selected implementation. Applications link its libpthread.a,
not the custom historical runtime in poc/pthread.c. This is GNU Pth's cooperative
POSIX.1c emulation API, not GNU libc/NPTL or a claim of full modern POSIX conformance.

## Reproducible build

Run inside the project's Ubuntu VM, from ~/src/pthreads-amix:

```sh
bash tools/build-gnu-pth.sh
sh tools/build-proof.sh
bash tools/build-pigz-gnu.sh
bash tools/package-gnu-pth.sh
```

The compiler is GCC 14.2.0 with AMIX ABI patches/specs; library and test builds
use -O2 -m68060 -std=gnu89 -fno-strict-aliasing. Pigz uses gnu99. Do not substitute
a generic m68k compiler or the native GCC 2.7 installation for acceptance.

Upstream: https://ftp.gnu.org/gnu/pth/pth-2.0.7.tar.gz

```text
SHA256 72353660c5a2caafd601b20e12e75d865fd88f6cf1a088b306a3963f0bc77232
```

Configure enables pthread emulation and static libraries, using mcsc/sc/mc:
getcontext, setcontext, makecontext, swapcontext. Enabling pthread emulation
also enables GNU Pth's soft syscall mappings. The selected stack convention
(base address plus size, downward growth) is checked by physical-machine probes.

The old generated-header make dependencies are incomplete. The script performs
an ordered clean build: configure, clean, apply fixes, generate pth_p.h, then
parallel compilation. Incremental rebuilding only pth_mctx.c does not update
all callers of its generated context-switch macros.

## Port fixes

tools/patch-gnu-pth.py applies three idempotent changes to the external source:

1. AMIX has process-global errno. Pth's mcsc switch macro originally used raw
   swapcontext, unlike its sjlj path, without saving/restoring errno. The AMIX
   path now saves the outgoing value and restores the incoming value; new
   contexts initialize their saved errno to zero.
2. The old AMIX headers have no complete struct timespec. Pth has a private
   fallback but only forward-declares the public type. The generated pthread.h
   receives the matching AMIX public fallback when configure reports no native
   type; the library's private definition remains unchanged.
3. Upstream pthread_once marks completion after invoking the callback. If the
   callback yields, peers can invoke it again: four workers produced four calls
   in our regression. The AMIX path now tracks initialization in progress,
   yields waiting peers, and registers Pth cleanup to reset the state if the
   initializer exits/cancels. Distinct nested once controls work. Waiting peers
   currently yield rather than sleep on a per-control condition, so a long
   blocking initializer can consume CPU through those waiters.

These are port changes to GNU Pth, retained under its LGPL terms. The SDK includes
the upstream source archive, patch script, generated diff, and COPYING files.
The original test/helper code has the MIT terms in poc/LICENSE.

## Physical-machine acceptance, 2026-10-28

Target uname identity: 68060-261027-ia, on the user's updated physical machine.
No kernel/module, system libc, boot, or system include installation was changed.
Artifacts execute in /tmp/pthreads-amix-261028/gnu with alarm watchdogs.

| Test | Observed result |
|---|---|
| Upstream test_std | All upstream standard tests passed, exit 0 |
| Upstream test_pthread | Four child threads, sleep/join lifecycle, exit 0 |
| Application proof linked only to GNU libpthread.a | 600 items, checksum 180300, once=1, cleanup=28, key destructors=4, 24 lifecycle repetitions, exit 0 |
| errno isolation | Distinct values survive cooperative yields |
| Wrapped pipe read | Reader waits while peer makes 100 progress steps |
| Timed condition wait | ETIMEDOUT and mutex reacquisition, exit 0 |
| Initializer exit and nested once | Initializer retries; distinct nested controls execute correctly, exit 0 |
| pigz 2.8, -p 2 -b 32 | 262144-byte regular input compressed; native AMIX gzip decompresses; cmp is identical, all exit 0 |

The initial unpatched errno and yielding-once failures are kept in the logs.
Acceptance is based on the final successful runs, not on source inspection alone.
Link maps show the GNU libpthread.a archive and no custom runtime object.
The 68060 instruction checker found no removed 64-bit multiply/divide forms in
the final acceptance executables.

Raw logs are under ignored local/logs: linux-live.log, amix-live.log, and the
earlier toolchain/cross-build logs. Remote commands are displayed before execution
in persistent visible terminals, controlled through tools/visible-session.ps1.

## SDK and application use

The physical-machine installation uses the simple root install.sh and the
/usr/local/gnu prefix: include/, lib/, bin/, man/, and share/doc/gnu-pth/.
It uses a 15-file manifest and byte comparisons, without the obsolete native
package manager. Publication is the source recipe and three patches only;
generated binaries, upstream source archives, and private sysroots stay out
of the publication repository/bundle. See BUILD-RECIPE.md.

The physical installation completed on 2026-10-28: all 15 files compared equal
to the staging payload, and repeating install.sh exited 0. The earlier
/usr/local bootstrap files and GNUpth registration were removed after checking
their original bytes and creating a backup. All unrelated package records
compared byte-for-byte equal before and after that one-time cleanup.

The private development artifact dist/gnu-pth-2.0.7-amix-m68060.tar.gz contains relocatable include/ and lib/
directories plus source, port changes, licences, examples, and SHA256SUMS.
It contains no licensed AMIX sysroot material.

To rebuild the library from the SDK's included source bundle, enter its
share/source directory and run (using the existing AMIX-adapted GCC 14 compiler):

```sh
PTH_SOURCE="$PWD/pth-2.0.7" CC=/path/to/m68k-elf-gcc \
    PTH_PREFIX="$HOME/opt/pth-amix" bash tools/build-gnu-pth.sh
```

With the AMIX-adapted compiler and its external sysroot:

```sh
m68k-elf-gcc -O2 -m68060 -I"$SDK/include" app.c \
    -L"$SDK/lib" -lpthread -ldl -lsocket -lnsl -o app
```

Include the GNU pthread.h when compiling application code to get its soft
read/write/select/sleep mappings. This is not automatic interception of I/O
inside arbitrary precompiled libraries. Avoid mixing headers or library objects
from another pthread implementation. SDK installation is private/explicit;
system /usr/include and libc are not replaced. The physical config helpers
report /usr/local/gnu; their build-host paths are not installed.

The compiler runtime was built with --disable-threads. This port does not by
itself enable libstdc++ std::thread, compiler TLS, or a C++ gthread backend.
Python 3 version selection and its full backend audit remain subsequent work.

GNU Pth does not preempt CPU-bound threads and cannot execute 68k work on multiple
cores. The physical tests cover the named paths, not every signal, cancellation,
fork, process-shared synchronization, realtime, or timeout interaction.
