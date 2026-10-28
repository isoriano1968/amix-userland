# Staged implementation plan

## Stage 0: target and source inventory

Use an ordinary account for read-only inspection once login is available. Root
is not required for the planned initial inventory. Record a minimal, sanitized
summary of uname/kernel identity, CPU/memory, relevant installed libraries,
headers, toolchain configuration, and available source or symbol information.

Audit a selected Python release's actual threading backend and pigz's pthread
calls. Produce an API/dependency matrix before selecting implementation scope.
Audit the GCC runtime's thread configuration as well as the compiler frontend.

Deliverable: measured baseline and an initial compatibility matrix.

## Stage 1: isolated capability probes

Cross-build small programs, keeping all output in a dedicated experiment
directory. Transfer and execute them only once the target login and build path
are established. Avoid broad filesystem scans and system modifications.

Probe separately:

- Context save/restore, stack alignment, callee-saved registers, floating-point
  registers, and floating-point control state. Use ucontext if supported; assess
  architecture-specific context code only if necessary.
- Signals and masks, timer delivery, interrupted syscalls, monotonic or fallback
  timeout clocks, and behavior during blocking pipe/socket reads.
- errno access, relevant libc global state, atomic code generation, and any
  libatomic dependencies. Prefer operations appropriate for the actual CPU.
- Existing kernel primitives for multiple execution contexts sharing ordinary
  private application memory, if any are found.

Deliverable: reproducible results sufficient to choose an execution model.

## Stage 2: execution-model decision

### User-space route

Evaluate cooperative threads as an interim compatibility subset. Measure the
cost and coverage of I/O wrappers. Document that CPU-bound code without yields
can starve peers and unwrapped blocking calls can stop all threads. A successful
context-switch test does not establish application compatibility.

Do not add signal-driven preemption merely to address starvation: first resolve
libc reentrancy, signal safety, and saved-context correctness. GNU Pth is a
candidate reference or port, subject to technical and licence review.

### Kernel-backed route

If suitable primitives already exist, evaluate them directly. Otherwise write
a bounded kernel interface proposal identifying required scheduler, VM, signal,
and process-lifecycle changes. A module may expose an interface later, but
module loading alone cannot establish those semantics. Implementation waits
for the necessary source/hooks and a suitable test environment.

Deliverable: architecture decision with measured prerequisites, known limits,
and an explicit explanation of which target applications it can support.

## Stage 3: minimum useful runtime

Provisional API, to be refined against application source:

- Thread creation, identity, exit, join, detach, and essential attributes.
- Mutexes, condition variables, once initialization, and thread-specific keys
  with destructors.
- Timed waits and per-thread errno handling appropriate to the execution model.

Publish semantics and unsupported features explicitly. Cancellation, fork,
process-directed signals, process-shared synchronization, realtime policies,
compiler TLS, and robust mutexes require separate decisions. Unsupported API
operations must not silently pretend success. Compiler TLS and pthread keys
are different facilities.

Meaningful acceptance tests include lost-wakeup races, condition predicates,
join/detach lifecycle, timeout behavior, key destructors, stack/FPU preservation,
and concurrent allocation/stdio. For a kernel-backed route, require one thread
to block on a pipe while another continues. An interim user-space route must
state which wrapped I/O paths provide that behavior.

## Stage 4: application acceptance

1. pigz: use at least two compression workers; verify decompressed bytes against
   the input, repeated runs, pipes, and error paths. Single-worker success is
   not threading acceptance. No multicore speedup is expected on one 68k CPU.
2. Python 3: select a release after the compiler/libc/API audit; first establish
   interpreter startup, then relevant thread/lock/condition/local-storage tests,
   blocking I/O interactions, and orderly interpreter shutdown. The GIL does
   not eliminate native synchronization or blocking-I/O requirements.
3. Meson/Ninja: assess native build capability after Python works. Host-side
   Meson/code generation and cross-compilation can proceed independently.
4. Mesa/Gallium: choose a specific release and minimal Softpipe configuration;
   audit platform integration, threading, atomics, TLS, and dependencies. Keep
   software rendering acceptance separate from hardware-driver development.

ImageMagick can be pursued independently with threading/OpenMP disabled for
early productivity. It is not a gate for this pthread runtime.

## Next concrete work

Prepare the read-only inventory script and release-specific application API
audit. Authenticate to the target when account access is provided. Do not
choose kernel changes or promise full POSIX conformance before the probe results.
