# pthreads-amix working instructions

- The user authorizes autonomous environment setup, implementation, and isolated
  hardware probes. Continue until there is a measured proof or a real decision;
  do not repeatedly ask for dependencies, permissions, or routine choices.
- Use GCC 14 cross-compilation with -m68060 for AMIX implementation acceptance.
  Native GCC 2.7 diagnostic results are not acceptance evidence.
- GNU Pth and its pthread emulation are the user-selected implementation.
  Keep poc/pthread.c historical; never link it into GNU acceptance or apps.
  Default scripts are build-gnu-pth.sh, build-proof.sh, build-pigz-gnu.sh.
- Use the project-local Ubuntu VM via tools/linux-vm.ps1. See
  docs/BUILD-ENVIRONMENT.md for paths and the pinned AMIX toolchain revision.
- The physical AMIX machine at 192.168.0.133 has many kernel updates. Its
  observed behavior is authoritative; public source alone is not its baseline.
  The observed uname identity is 68060-261027-ia. Do not equate a filename or
  uname string with byte-for-byte proof of a loaded kernel image.
- Give the user real logs and/or screenshots with progress updates. Keep raw
  build/target logs under ignored local/logs and link relevant excerpts.
- Always open a visible terminal for remote logins and remote commands. Use
  tools/visible-session.ps1 launched with a normal visible window and -NoExit;
  do not perform hidden remote sessions. Show commands before execution.
- Credentials must not be committed, printed, or written into source/log files.
  The Telnet adapter accepts credentials only from the process environment.
- Keep target experiments under /tmp/pthreads-amix-261028 or another dedicated
  scratch directory. No system libc, kernel, boot, or compiler replacement is
  part of the initial proof.
- Do not claim POSIX conformance or blocking-I/O isolation from cooperative
  scheduling tests. Record exact compiler flags, artifact hashes, and limits.
- No licensed sysroot material or generated vendor-header copies enter Git.
- Install GNU software for this task under /usr/local/gnu using root install.sh,
  not the obsolete AMIX package manager. Publish the source recipe and patches
  only, never binary libraries or a bundled AMIX sysroot.
