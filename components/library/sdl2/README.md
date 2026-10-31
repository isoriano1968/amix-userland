# sdl2-amix

SDL 2.30.12 for Commodore Amiga UNIX (AMIX, SVR4, m68k): a static `libSDL2.a`
for 68040/68060 machines, cross-built with gcc14-cross-amix against X11R6.3 and
[gnu-pth-amix](https://github.com/isoriano1968/gnu-pth-amix). SDL itself is not
here: `fetch.sh` downloads the release and checks its hash, `build.sh` patches it.

- `patches/`, applied in order:
  audio driver, pth threads and UTF-8, no dynamic API, fullscreen under dtwm by
  Motif hints, YUV in the software renderer, pth priorities, nearest scaling
  for 16-bit surfaces, the zz9k renderer.
- `amix/`, laid over the source: the audio driver (Paula), the threads, the
  `zz9k` renderer (ZZ9000/Z3660 RTG: textures in board memory, drawn as soft3D
  triangles), and `mkconfig.py` for `SDL_config.h`.
- `test/`: small programs for the threads, events and renderers.

## Building

    sh fetch.sh
    SHM_LIBS=<dir> sh build.sh

The result is in `build/install/usr/local`. `AMIX_SYSROOT` defaults to
`~/opt/m68k-amix-sysroot`. `SHM_LIBS` names a directory with a static `libXext.a`
that has XShm, and `libshmsys.a`; `PTH_LIB` another build of gnu-pth.

`SDL_RENDER_DRIVER=software` turns the zz9k renderer off. It falls back to
software by itself when soft3D is busy or missing.

SDL is under the zlib licence.
