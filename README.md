# pthreads-amix

GNU Pth 2.0.7 and its POSIX pthread emulation for updated Amiga UNIX (AMIX),
cross-built with GCC 14.2 and -m68060. This project carries the build scripts,
small AMIX port fixes, and hardware acceptance tests.

## Status

GNU Pth builds and its upstream standard and pthread tests run on the physical
machine reporting `68060-261027-ia`. See [GNU Pth port notes](docs/GNU-PTH.md)
for current acceptance results and limitations. No module loader or kernel
change is needed for this user-space library.

GNU Pth is the implementation. The custom [poc](poc/README.md) runtime is a
historical experiment, not the library applications should link. The default
acceptance build links the GNU archive and never links poc/pthread.c.
The project has its own [Ubuntu build VM](docs/BUILD-ENVIRONMENT.md).

## Objective

Provide useful, explicitly documented pthread compatibility for existing AMIX
software, with Python 3 as the strategic application target. Use small tests
and threaded pigz compression as earlier acceptance milestones. Newer
Mesa/Gallium is a subsequent integration target, not an initial acceptance gate.

## Approach

1. Build the pinned GNU Pth release and apply the measured AMIX fixes.
2. Run upstream and application tests on the physical machine.
3. Package its headers and static libraries for cross-builds.
4. Audit a selected Python 3 release against the actual supported API.

GNU Pth is cooperative and provides pthread emulation; using an established
library is not a claim of full modern POSIX conformance. Wrapped I/O support
requires applications to be compiled with its header mappings.

Inside the Linux VM, from the project directory:

```sh
bash tools/build-gnu-pth.sh
sh tools/build-proof.sh
bash tools/build-pigz-gnu.sh
```

The physical install uses [install.sh](install.sh) under **/usr/local/gnu**,
with headers, static libraries, config helpers, manual pages and a manifest.
No native AMIX package manager is required. For publication, use the
[source build recipe](docs/BUILD-RECIPE.md) and [patches](patches/README.md);
compiled libraries and private sysroots are excluded.

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
- GNU Pth and its port modifications retain GNU Pth's LGPL terms. The original
  custom proof has its own MIT licence; do not confuse the two implementations.

This is an independent local Git repository. It has no configured remote.
