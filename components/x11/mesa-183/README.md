# Mesa 18.3.6 for AMIX

The system's OpenGL: `libGL.so.18`, `libGLU.so.18` and `libglut.so.18` in
`/usr/local/lib`, with headers and `.pc` files. GL 3.1 compatibility, GLSL 1.40.

Mesa runs on the 060 and draws through `zz3660`, a Gallium driver that sends
its work over the Z3660's ring to softpipe on the board's second ARM core, which
shows frames in the RTG overlay. Without that core it falls back to softpipe on
the 060. A driver for a future GPU only has to replace `zz3660`.

`make prep` fetches the release and applies `patches/`; `build-package.sh STAGE`
builds the package. The core 1 program, its build (`build/xbuild.py`) and
`libzzgl`, the fast path for games and intros, are in
[core1](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/firmware/core1).

One program at a time can use core 1. A program's first window uses the overlay;
further windows draw through X. The overlay always sits on top of X, so menus
and other windows cannot appear over a GL window. Transform feedback is accepted
but captures nothing, and there are no compute shaders or shader images.

GL programs need more address space: `ulimit -d 98304 -v 131072`.

Mesa is under the MIT licence.
