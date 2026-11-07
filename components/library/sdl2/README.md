# sdl2-amix

SDL 2.30.12 for Commodore Amiga UNIX (AMIX, SVR4, m68k): the shared
`libSDL2-2.0.so.0` in /usr/local/lib for 68040/68060 machines, cross-built with
gcc14-cross-amix against the shared X11R6.3 and `components/library/pth`. SDL itself is not
here: `fetch.sh` downloads the release and checks its hash, `build.sh` patches it.

- `patches/`, applied in order:
  audio driver, pth threads and UTF-8, no dynamic API, fullscreen under dtwm by
  Motif hints, YUV in the software renderer, pth priorities, nearest scaling
  for 16-bit surfaces, the zz9k renderer, the HDMI audio driver, and
  `Requires: x11 pth` in sdl2.pc.
- `amix/`, laid over the source: the audio driver (Paula), the threads, the
  `zz9k` renderer (ZZ9000/Z3660 RTG: textures in board memory, drawn as soft3D
  triangles), MIT-SHM's shm* system calls (libc.so.1 lacks them), and
  `mkconfig.py` for `SDL_config.h`.
- `test/`: small programs for the threads, events and renderers.

## Building

    make build stage        # build/sdl2-amix.tar
    make sysroot-install    # into AMIX_SYSROOT (default ~/opt/m68k-amix-sysroot)

Every file is built `-fPIC -fvisibility=hidden` into `build/libSDL2.a`, and
`tools/amix-mkshlib` makes `libSDL2-2.0.so.0` of it; it exports SDL's API and
nothing else. As upstream, `libSDL2.so -> libSDL2-2.0.so -> libSDL2-2.0.so.0`;
on AMIX `amix-stubs` replaces `libSDL2-2.0.so` with the link stub for AMIX's ld.
There is no `libSDL2.a`.

Programs take their flags from `pkg-config sdl2` (`tools/amix-pkg-config` when
cross-building): `-lSDL2` with x11 and pth, so the program names libsocket
(whose syslog and seteuid come from libc's archive) and gets the X and Pth
directories in its runpath. `sdl2-config` is there for native builds.

`SDL_RENDER_DRIVER=software` turns the zz9k renderer off. It falls back to
software by itself when soft3D is busy or missing.

SDL is under the zlib licence.
