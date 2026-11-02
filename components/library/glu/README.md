# GLU for AMIX

The SGI GLU 9.0.3 as `libGLU.so.1` in `/usr/local/lib`, with `GL/glu.h` and `glu.pc`. Needs Mesa's `libGL`.

AMIX's `size_t` is `unsigned int`, so the build changes one header: `operator new` wants the compiler's type.
