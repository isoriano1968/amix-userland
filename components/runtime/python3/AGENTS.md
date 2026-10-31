# Python 3 AMIX port instructions

- Use the AMIX-adapted GCC 14.2 cross-compiler with -m68060 for every target
  object/probe. Native Linux compilation is only for generation tools and
  clearly identified host logic tests.
- GNU Pth is the pthread implementation; never use the sibling custom runtime.
- Use Linux builds. The current VM/toolchain live under the sibling
  pthreads-amix project; see its docs/BUILD-ENVIRONMENT.md.
- Use visible terminals and saved logs for remote commands. The sibling
  tools/visible-session.ps1 supports this. Credentials stay process-local.
- AMIX's native utilities need legacy syntax: tail -6, xargs -n16. Test a
  directory before mkdir -p, which can fail if its final directory exists.
  The original Bourne shell has no command-negation `!` syntax.
- Use a hard link for python3-config: the current kernel mishandles execution
  through a relative symlink to its shell script. Binary symlinks work.
- Publish recipe, patches, adapters and tests only. Exclude passwords, keys,
  licensed AMIX headers/libraries, binaries, VM files and kernel inventories.
- Intended remote: https://github.com/isoriano1968/amix-userland/tree/main/components/runtime/python3.
- Target proof: /tmp/pthreads-amix-261028/python/py311. Do not replace libc,
  system Python, kernel or boot files during these probes.
- The user authorizes installation of this Python port under /usr/local using
  install.sh: bin, lib, include and man. GNU Pth stays under /usr/local/gnu.
- The physical kernel has extensive updates; measured behavior is authoritative.
  The owner confirmed /stand/unix.z3 is currently booted (2026-10-28).
  /unix is an older image; its symbols do not describe the running kernel.
- Normal startup needs a functional random device. PYTHONHASHSEED=0 is explicit
  for controlled tests, never an implicit fallback or production default.
- Read README.md and docs/PORT-NOTES.md for tested scope and limitations.
