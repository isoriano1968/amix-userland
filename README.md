# pthreads-amix

An experimental POSIX threads runtime for Amiga UNIX (AMIX), developed against
the community-maintained 68040/68060 kernel and GCC 14 application toolchain.

## Status

Planning and capability discovery. No pthread implementation exists here yet.
The current loadable-module implementation is unavailable to this project.
No kernel thread, shared-address-space task, or libc reentrancy capability has
been verified on the target.

## Objective

Provide useful, explicitly documented pthread compatibility for existing AMIX
software, with Python 3 as the strategic application target. Use small tests
and threaded pigz compression as earlier acceptance milestones. Newer
Mesa/Gallium is a subsequent integration target, not an initial acceptance gate.

## Approach

1. Inventory the running system and available headers, libraries, and kernel
   interfaces without changing the target.
2. Build isolated probes with GCC 14 and establish which facilities actually
   work, including context switching, signals, timing, and blocking I/O.
3. Choose the execution model from measured results: user-space threads,
   kernel-backed threads, or a deliberately limited interim implementation.
4. Implement and test a small pthread API with an explicit compatibility matrix.
5. Validate real applications before widening the API or claiming conformance.

The module loader is not a prerequisite for the first two stages. No route is
assumed to deliver full POSIX semantics before its prerequisites are measured.

See [the roadmap](docs/ROADMAP.md), [baseline](docs/BASELINE.md), and
[initial architecture decisions](docs/DECISIONS.md).

## Development rules

- Keep compiler/ABI compatibility, libc safety, and scheduling correctness as
  separate questions; success in one does not establish the others.
- Record exact source revisions, compiler flags, kernel identity, and target
  results for every acceptance claim.
- Store target output and small reproducers with each finding; distinguish
  source inspection, emulator results, and real-hardware results.
- Do not commit passwords, licensed AMIX binaries/headers, copied system
  libraries, or complete target filesystem inventories.
- Keep experimental builds outside system library paths. Do not replace libc,
  install a system pthread library, or change the running kernel during probes.
- Select a licence before introducing runtime implementation or imported code.

This is an independent local Git repository. It has no configured remote.
