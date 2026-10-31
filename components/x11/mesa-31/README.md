# Mesa 3.1 for AMIX

An independent Mesa 3.1 software-rendering port for Commodore Amiga UNIX
(AMIX), using X11R6.3 and the MNT ZZ9000 framebuffer X server.

The repository contains only the AMIX port overlay and deterministic source
installer. Unmodified Mesa sources come from the official Mesa archive.

## Layout

- AMIX build tree: `/usr/mesa3/Mesa-3.1`
- X11 dependency: `/usr/x11r6`

Mesa is intentionally independent from the X11R6 source tree. The initial
target uses Mesa's Xlib software driver, so it does not require a server-side
GLX extension or ZZ9000 3D acceleration.

## Prepare sources

On a host with HTTPS access, `gzip`, `tar`, and a SHA-256 utility:

```sh
git clone https://github.com/isoriano1968/amix-userland.git
cd amix-userland/components/x11/mesa-31
sh install.sh /export/amix/mesa3
```

Copy or mount `/export/amix/mesa3/Mesa-3.1` as
`/usr/mesa3/Mesa-3.1` on AMIX.

## Initial AMIX build

The upstream Mesa 3.1 distribution already contains an historical AMIX
target. This overlay keeps that target but selects GCC 2.7.2.3 and the
independent X11R6 tree. MIT-SHM, pthreads, CPU assembly, and hardware drivers
are not enabled.

```sh
cd /usr/mesa3/Mesa-3.1
sh amix-build.sh libs
sh amix-build.sh xdemos
sh amix-build.sh mech
```

The first milestone builds static Mesa, GLU, and GLUT libraries to validate
the compiler and Xlib renderer. Native AMIX shared libraries will follow only
after the software renderer and demos work correctly.

For runtime tests against the ZZ9000 server:

```sh
DISPLAY=:1
LD_LIBRARY_PATH=/usr/x11r6/xc/exports/lib:/usr/lib
export DISPLAY LD_LIBRARY_PATH
```

The `mech` target builds the classic GLUT robot demo imported from GLUT 3.7:

```sh
cd /usr/mesa3/Mesa-3.1
sh amix-build.sh mech
./demos/glutmech
```

## The 68060 build with soft3D (Z3660)

For a 68060 with a Z3660, the same overlay also builds a second variant,
cross-compiled on a Linux host with
[gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix):

- **soft3D.** On the Z3660, Mesa hands every triangle, line and point to the
  board's own rasterizer (soft3D, Wazp3D on the Z3660's ARM). The 68060 only
  transforms, lights and clips. The frame is drawn in off-screen VRAM, and
  the X server copies it into the window through its clip list. Without the
  Z3660 driver or the server extension, Mesa draws in software as before.
  `MESA_SOFT3D=0` turns it off; `MESA_SOFT3D=1` says why it could not be
  used.
- **MIT-SHM** for the back buffer, aligned for the server's `move16` copy.
- **68060 code,** with `move16` buffer clears. `MESA_NOMOVE16` turns those off.

What it needs:
- the 68040/68060 kernel from
  [amix-040-060-port](https://github.com/asokero/amix-040-060-port) (MIT-SHM
  is only safe there);
- the Z3660 RTG driver built with `-DZZ9K_MAP_BOARD`, and the `Xzz9000`
  server with the ZZ9K-SOFT3D extension. Both are in
  [soft3d-z3660rtg-amix](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/uts/amiga/io/soft3d);
  [x11r6.3-amix](https://github.com/isoriano1968/amix-userland/tree/main/components/x11/x11r6.3) carries the
  same server.

Building, on the Linux host, in the prepared tree:

```sh
export PATH=$HOME/opt/m68k-amix/bin:$PATH     # gcc14-cross-amix
AMIX_SYSROOT=$HOME/opt/m68k-amix-sysroot sh amix060-build.sh demos
```

That gives static `lib/libGL.a`, `libGLU.a` and `libglut.a`, and the demos in
`demos/bin060/`. Programs link them statically. For MIT-SHM, put a static
`libXext.a` with the XShm client functions first, and add `-lshmsys`.
`demos/build060.sh` shows the link line; its `SHM_LIBS` names where those
libraries are.

Measured on an A3000 with a Z3660 68060 at 80 MHz: gears went from 6.6 fps
(software, before MIT-SHM) to 10.6 with MIT-SHM. soft3D rasterizes at about
20 Mpixel/s, against 3.4 for the 68060 in software.

The native build with gcc 2.7.2.3 (`amix-build.sh`) is unchanged. The soft3D
files compile to nothing there (`AMIX_SOFT3D` is only set by the `amix060`
target).

## Source archives

- `MesaLib-3.1.tar.gz`
- `MesaDemos-3.1.tar.gz`
- `glut-3.7/progs/demos/glutmech/glutmech.c`

The Mesa archives are downloaded from the
[official Mesa archive](https://archive.mesa3d.org/older-versions/3.x/).
The `glutmech` demo comes from the official GLUT 3.7 archive; its original
source header and GLUT notice are kept in the overlay.
