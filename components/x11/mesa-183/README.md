# Mesa 18.3.6 for AMIX

The system's OpenGL: `libGL.so.18` in `/usr/local/lib`, with headers and
`.pc` files. GLU and GLUT are [library/glu](../../library/glu) and
[library/freeglut](../../library/freeglut). GL 3.1 compatibility, GLSL 1.40.

Mesa runs on the 060 and draws through `zz3660`, a Gallium driver that sends
its work over the Z3660's ring to softpipe on the board's second ARM core, which
shows frames in the RTG overlay. Without that core it falls back to softpipe on
the 060. A driver for a future GPU only has to replace `zz3660`.

The build takes the release and amix-gate's
[core1](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/firmware/core1)
at a pinned commit (Mesa's patch, `build/xbuild.py`, `libzzgl`, the fast path for
games and intros), with `patches/` for its compiler paths, into `build/core1`.
Mesa's own meson build for the PC gives xbuild.py the sources and the generated
files; it needs meson, ninja and Mako (`python3-mako`) on the build machine.
`make build` makes libGL.so.18 and libzzgl.so.1 for AMIX; `make arm` makes core 1's
`build/core1/build/arm/libOSMesa.a`, which games/ioquake3 and games/iortcw link.

One program at a time can use core 1. A program's first window uses the overlay;
further windows draw through X. The overlay always sits on top of X, so menus
and other windows cannot appear over a GL window. Transform feedback is accepted
but captures nothing, and there are no compute shaders or shader images.

GL programs need more address space: `ulimit -d 98304 -v 131072`.

Mesa is under the MIT licence.
