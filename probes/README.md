# Context probes

Run in a dedicated scratch directory, never a system library/source directory.
Each executable arms a five-second alarm. These programs can fail or crash;
they do not change system configuration or install a runtime.

Initial native builds on the physical machine:

```sh
cc -O0 context-roundtrip.c -o context-roundtrip
./context-roundtrip
echo $?
cc -O0 context-stack.c -o context-stack
./context-stack
echo $?
```

Record the actual compiler version and running kernel with every result.
Repeat using the GCC 14 cross-toolchain before adopting an ABI or context
implementation. A native GCC 2.7 pass is not a GCC 14 acceptance result.

context-roundtrip tests basic getcontext/setcontext control-flow restoration.
context-stack tests makecontext and a swapcontext roundtrip on supplied stack
storage, verifying a local variable is inside that storage. Neither tests
general register/FPU preservation, stack arguments, uc_link return behavior,
guard pages, errno isolation, libc safety, or signal-mask restoration.
