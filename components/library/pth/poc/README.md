# Cooperative runtime proof

Status: historical custom proof, tested successfully on AMIX with GCC 14.
GNU Pth is now the user-selected implementation; see ../docs/GNU-PTH.md.
Do not develop this runtime as the application library. proof.c is also reused
as a test harness with AMIX_USE_GNU_PTH, linking only GNU's libpthread.a.
Do not interpret the presence of pthread.h as a conformance claim.

This source is an isolated experiment, not a system-installed libpthread or a
stable ABI. The original implementation in this directory is MIT licensed.
It uses libc ucontext, with no kernel/module changes and no timer preemption.

## Build and acceptance

From the repository root, on the GCC 14 cross-build host:

```sh
CC="$HOME/opt/m68k-amix/bin/m68k-elf-gcc" sh tools/build-custom-proof.sh
```

The build script requires GCC 14 and uses -O2 -m68060. The project has an
isolated Ubuntu build VM; see ../docs/BUILD-ENVIRONMENT.md. Native GCC 2.7
observations remain diagnostic evidence only. Use the GCC 14 toolchain's
68060 instruction checker before deployment.

Run the context probes and pthread-proof separately on the physical target.
Every proof has an alarm bound. pthread-proof expects four worker threads,
600 heap-allocated producer/consumer items, checksum 180300, one once callback,
28 cleanup calls, four key destructors, and 24 worker-slot reuse cycles.
The mutex is deliberately held across some yields to exercise contention.

## Implemented experiment

- Bounded cooperative round-robin scheduler, 12 contexts including main,
  64 KiB heap stacks, joinable threads only, monotonically assigned IDs.
- create/join/exit, identity, minimal joinable attributes.
- Mutex ownership and blocking; condition wait/signal/broadcast with no
  scheduling window between releasing the mutex and parking.
- once with waiters, cleanup push/pop, thread-specific keys with up to four
  destructor passes, and per-context save/restore of libc errno.

Only explicit yield, synchronization waits, join, and exit switch contexts.
libc calls do not contain runtime-inserted switch points, so this proof avoids
asynchronous reentry. This does not establish general libc thread safety.

## Explicit limits

- No preemption: CPU work must yield or reach synchronization waits.
- No blocking-I/O isolation: a blocking syscall stops the whole process.
- No timed waits, cancellation, detached threads, key deletion/reuse, stack
  attributes, compiler TLS, process-shared locks, fork handlers, or realtime
  policies. Unsupported detach attributes are rejected, not silently accepted.
- No guard pages or stack-overflow recovery.
- Deadlock without runnable contexts is detected as EDEADLK on waits, or a
  diagnostic process exit if encountered during pthread_exit. This is a proof
  diagnostic and is not a general POSIX deadlock-detection guarantee.
- No binary compatibility with any future pthread implementation. Include this
  header only using a private -Ipoc build; never copy it into system includes.
- The main context must return normally; joining main and calling pthread_exit
  from main are outside this proof's supported lifecycle.

Before any real application acceptance: GCC 14 hardware runs, error-path and
resource-limit tests, full application API audit, and I/O-model selection.
