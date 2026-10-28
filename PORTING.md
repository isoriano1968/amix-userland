# AMIX port notes

## Baseline

- Mesa 3.1
- GCC 2.7.2.3 on AMIX
- X11R6.3 headers and libraries from `/usr/x11r6`
- 16-bit TrueColor ZZ9000 X server
- Software rendering through Mesa's Xlib driver

The prepared source replaces upstream's multi-platform `Make-config` with an
AMIX-only version. Old AMIX `make` parses every included rule and otherwise
fails on malformed backslashes in unrelated upstream platform targets.

## Deliberate exclusions

These apply to the native build (`amix-build.sh`). The `amix060` cross build
adds soft3D on the Z3660 and MIT-SHM (see README.md).

- No ZZ9000 hardware acceleration
- No MIT-SHM
- No pthread or Solaris-thread backend
- No x86 or other CPU-specific assembly
- No DRI
- No server-side GLX extension in the first milestone

## Build sequence

1. Build `src` to produce `libGL.a` with the Xlib driver.
2. Build `src-glu` to produce `libGLU.a`.
3. Build `src-glut` to produce `libglut.a`.
4. Build `xdemos` and run a minimal visual test against `Xzz9000`.
5. Build selected GLUT demos such as `gears` and `glutmech`.
6. Add AMIX ELF shared-library rules only after the static path is proven.

The X11R6 repository owns any future server-side GLX extension. This Mesa
repository owns the renderer, GL/GLU/GLUT libraries, demos, and client-side
GLX/Xlib integration.

`glutmech` is imported from GLUT 3.7 as a useful animation-heavy GLUT test.
It intentionally stays in `demos` beside `gears` instead of being folded into
the X11R6 tree.
