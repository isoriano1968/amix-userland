# Initial application API audit

2026-10-28. Source inspection only. Neither application has been built or run
against an AMIX pthread runtime. These are bounded file audits, not complete
application dependency manifests or a commitment to a Python release.

## pigz 2.8

Source: `madler/pigz`, tag `v2.8`, `yarn.c`.
Git blob: `e0762580b1ac17a9ca0f7fcf056c705b9605bd12`.
https://github.com/madler/pigz/blob/v2.8/yarn.c

Observed API surface:

- pthread_create, pthread_join, pthread_self, pthread_equal.
- pthread_attr_init, pthread_attr_destroy, pthread_attr_setdetachstate with
  PTHREAD_CREATE_JOINABLE.
- pthread_mutex_init/destroy/lock/unlock and PTHREAD_MUTEX_INITIALIZER.
- pthread_cond_init/destroy/wait/broadcast and PTHREAD_COND_INITIALIZER.
- pthread_cleanup_push/pop; pop(1) executes the cleanup on the normal path.

Important correction to a raw identifier scan: pthread_cancel appears in
comments/history but is not called by yarn.c. Cleanup handlers still need real
semantics; they cannot simply be empty macros. Inspect the rest of pigz before
claiming this list covers the complete application.

Additional full-application inspection of the verified pigz 2.8 release:

- `try.c` blob d949e4e7b1716fa2830b969e739828986cc4dffd.
- `try.h` blob ff06d74018834b086ae307fb0095c3b42f0fdc37.
- These add pthread_once, pthread_key_create, pthread_getspecific, and
  pthread_setspecific for per-thread exception stacks. No compiler TLS is
  required by this path. The proof runtime already implements these APIs.
- `pigz.c` itself no longer calls pthread functions directly; thread operations
  are routed through yarn, and try uses pthread-specific keys.
- Release tar SHA256:
  eb872b4f0e1f0ebe59c9f7bd8c506c4204893ba6a8492de31df416f0d5170fd0.
- Audit remaining platform dependencies separately: inttypes.h, va_copy,
  timestamp functions, filesystem APIs, and C99 formatting. Build without
  Zopfli for the initial regular-file compression proof.

## CPython 3.10.16 investigation sample

Source: `python/cpython`, tag `v3.10.16`, `Python/thread_pthread.h`.
Git blob: `35b9810aa377f25dfedf505400e5cba916d0423f`.
https://github.com/python/cpython/blob/v3.10.16/Python/thread_pthread.h

Observed core groups:

- Thread creation, detach, self, exit; attributes including stack size.
- Mutex init/destroy/lock/unlock/trylock.
- Condition init/destroy/wait/timedwait/signal.
- Thread-specific keys: create/delete/get/set.

Conditional facilities must be selected by honest configure results:

- Locks can use POSIX semaphores OR the mutex/condition fallback. Semaphores
  are not inherently required by this backend.
- Condition-clock attributes and CLOCK_MONOTONIC are optional paths.
- pthread_attr_setscope is conditional on PTHREAD_SYSTEM_SCHED_SUPPORTED.
- pthread_sigmask has a sigprocmask fallback in this file. That fallback does
  not prove correct per-thread signal behavior for an arbitrary runtime.
- Nonportable thread-ID functions are platform-specific, not initial AMIX APIs.

Still to inspect: GIL/condition abstraction, signal handling, fork hooks,
configure detection, extension modules, timing conversion and timeout behavior,
and exact libc/compiler requirements. The backend alone is not a full audit.

## Scope implication

Add cleanup handlers to the provisional pigz API. Keep timed conditions and
thread-specific keys in the Python milestone. Do not require every identifier
found in comments or inactive preprocessor branches.
