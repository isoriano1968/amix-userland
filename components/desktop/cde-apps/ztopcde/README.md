# ztopcde-amix

The Z3660 in a CDE window on AMIX: the board's state, and the programs of its
second ARM core (`z3660ARM_Core_*.bin` in `/root/z3660ARMCores`), started with
`armbench`.

`install.sh` puts it in place on the box. `S98z3660core` loads the default
program at boot. `ztopcde.fp` is a front panel control of its own;
`cde/amix.fp` in [vplay-amix](https://github.com/isoriano1968/vplay-amix)
replaces it with an AMIX subpanel.
