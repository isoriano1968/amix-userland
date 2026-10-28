# Architecture decisions

## 0001: discover before selecting a scheduler

Status: accepted, 2026-10-28.

We will not assume stock AMIX limitations describe the updated target, nor
assume a module loader provides shared-address-space threads. Initial work
consists of inventory and independent capability probes. The execution model
remains undecided.

## 0002: Python is the strategic target; pigz is an early test

Status: accepted as project direction, 2026-10-28.

Python offers scripting and build-system benefits. Its dependency surface is
too broad to isolate early scheduler bugs efficiently. Small tests and threaded
pigz therefore precede Python acceptance. Exact application versions remain
undecided until their requirements have been audited.

## 0003: preserve existing AMIX installations and ABI boundaries

Status: accepted, 2026-10-28.

Application experiments use separate build and deployment directories. No
system libc replacement, global compiler change, kernel installation, or boot
change is part of the initial discovery stage. Application and kernel objects
may need different toolchains. Credentials and vendor-owned sysroot material
stay outside Git.

## 0004: GCC 14 with 68060 code generation is required for acceptance

Status: accepted, 2026-10-28, explicit user direction.

Use the GCC 14 AMIX cross-toolchain and -m68060 for implementation builds and
hardware acceptance. The target login's GCC 2.7.2.3 was used only for initial
diagnostics; its presence is not a reason to use it for the runtime. The
build-proof script rejects other compiler versions. Existing native results
remain useful observations but must be repeated with GCC 14.

## 0005: isolated cooperative proof before I/O-model selection

Status: experimental, 2026-10-28.

Prepare a privately included pthread header and bounded runtime, using the
already observed ucontext facilities. No asynchronous preemption or blocking
syscall interception is introduced. The proof exercises producer/consumer
queues, contention, lifecycle, and per-context state. Success will establish
only cooperative API feasibility. It will not settle the kernel-thread design
or unblock arbitrary Python/pigz workloads without an explicit I/O audit.

The original proof runtime is MIT licensed under poc/LICENSE. No upstream
runtime implementation was imported.

## 0006: use GNU Pth as the application implementation

Status: accepted, 2026-10-28, explicit user direction.

Build GNU Pth 2.0.7 and its pthread emulation with GCC 14 and -m68060. The custom
runtime is historical proof only. Reuse tests against the GNU archive, with no
custom runtime object on the link line. Keep AMIX fixes small, reproducible,
reviewable, and distributed with upstream source/licence information.

The implemented port fixes errno switching, the public timespec fallback, and
yielding pthread_once initialization. See GNU-PTH.md for measured failures,
successful regressions, and remaining limits. This supersedes the custom
runtime direction in 0005 without claiming full POSIX conformance.
