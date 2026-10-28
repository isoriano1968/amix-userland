# Baseline and evidence

Recorded 2026-10-28. Repository documentation describes capabilities; it does
not establish which exact revisions are installed on the running machine.

## Established context

- AMIX has a community-maintained 68040/68060 kernel update line.
- GCC 14 is available for AMIX application development. The toolchain documents
  working C++, exceptions, RTTI, standard containers, and a C99 compatibility
  layer on real hardware. GCC 2.7.2.3 is not our application compiler baseline.
- The modern toolchain uses an AMIX-adapted m68k-elf target. Kernel and driver
  object compatibility must be assessed separately; the older native-target
  toolchain remains relevant there.
- The user reports loadable-module support, but its implementation is currently
  unavailable. Its hooks and capabilities are unknown.
- The target Telnet endpoint is 192.168.0.133:23. Authenticated inspection on
  2026-10-28 established running kernel identity `68060-261027-ia`. The physical
  machine includes additional updates and is the authoritative baseline; the
  public repository alone does not describe all installed capabilities.
- Native `cc` and `gcc` in this login resolve to GCC 2.7.2.3. This is separate
  from the available GCC 14 cross-toolchain, whose local installation/build
  environment has not yet been located.
- Initial native-compiler context probes passed. See RESULTS-2026-10-28.md.

## References

- Kernel port: https://github.com/isoriano1968/amix-040-060-port
- Period-compatible toolchain: https://github.com/isoriano1968/gcc-cross-amix
- Modern application toolchain: https://github.com/asokero/gcc14-cross-amix
- CPython pthread backend, for release-specific auditing:
  https://github.com/python/cpython/blob/3.10/Python/thread_pthread.h
- pigz: https://www.zlib.net/pigz/
- GNU Pth, a possible user-space implementation reference:
  https://www.gnu.org/software/pth/pth-manual.html

The CPython link is an investigation reference, not a selected target release.
Pin release tags and commit hashes when actual implementation work begins.

## Unknowns to resolve

- Running kernel build, CPU, available memory, installed compatibility libraries,
  compiler location, and cross-build host access.
- Any existing pthread, LWP, or shared-address-space process interfaces.
- ucontext availability and correctness, including m68k register/FPU state.
- Signal-frame behavior, signal masks, timer facilities, and stack allocation.
- Whether errno is global and how libc wrappers access it.
- Reentrancy of allocation, stdio, resolver calls, and runtime loader paths.
- GCC gthread configuration, libgcc unwinder locking, libstdc++ synchronization,
  compiler TLS support, and atomic operations/runtime helpers on the target CPU.
- Available mechanisms for per-thread blocking and race-free sleep/wakeup.

Do not infer missing kernel facilities from stock SVR4 documentation alone.
