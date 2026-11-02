# freeglut for AMIX

freeglut 3.4.0 as `libglut.so.3` in `/usr/local/lib`, with `GL/glut.h`, `GL/freeglut.h` and `glut.pc`. It replaces the GLUT of Mesa 3.1.

`files/config.h` replaces the CMake step: X11R6.3 has no XInput2, Xrandr or XF86VidMode, so those are off, and with them game mode's resolution changes. `select` and `nanosleep` are mapped to `poll`, because AMIX keeps `select` in the static part of libc, which a shared library cannot use.
