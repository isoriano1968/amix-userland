# Mesa 18.3.6 for AMIX

Mesa 18.3 with softpipe on the Z3660's second ARM core: NEON fast paths for
GL1 games (`sp_fast.c`), driven by GL calls from the 060. `make prep` fetches
the release and applies `patches/`. The server around it, its build
(`build/xbuild.py`) and the 060's library are in
[core1-z3660-amix](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/firmware/core1).

Mesa is under the MIT licence.
