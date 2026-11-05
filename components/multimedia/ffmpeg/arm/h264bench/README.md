`zzav.h`, `bench.h` and `core1_net.h` are the interface to core 1's player
(and to its own network: amixcast reads whether core 1 fetches streams), copied from
[core1-z3660-amix](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/firmware/core1)
(`cores/h264bench`), so that `src/zzav.c` builds here. Keep them in step.
