# pt2-clone on AMIX

8bitbubsy's ProTracker 2.3D clone, running on **AMIX** (Commodore's Amiga
UNIX) under X11R6.3. It was developed on an Amiga 3000 with a Z3660 (68060
at 80 MHz) and its RTG output.

## What makes it work on AMIX

- **sdl2-amix** (`sdl2/`). pt2-clone is written for SDL 2, which AMIX doesn't
  have. sdl2-amix implements the part of SDL 2 that pt2-clone uses, on Xlib:
  - only the screen rows that changed are sent to the X server, through
    MIT-SHM where possible;
  - the X cursor is the mouse pointer;
  - keys are mapped by position;
  - sound is pumped from the main loop, because AMIX has no threads.
- **Sound through Paula itself.** With
  [paula-amix](https://github.com/isoriano1968/paula-amix)'s `/dev/paula`:
  - pt2-clone hands its Paula register writes to the kernel, one batch per
    replayer tick, and Paula plays the samples from chip RAM, as on an Amiga;
  - the CPU mixes nothing: audio takes 3–5 % of the time instead of 19–41 %;
  - the sound is Paula's own, with real hardware stereo.

  Without `/dev/paula`, pt2-clone mixes as usual and plays through
  `/dev/audio`. An integer mixer is the default there; it costs half of what
  the float one does on a 68060.
- **AMIX's gaps** (`compat/`): C99 maths functions AMIX's libm lacks,
  `strncasecmp`, and word-wise `memcpy`/`memmove`/`memset`/`memcmp`. AMIX's
  own are byte loops at 4–9 MB/s.
- **Changes to pt2-clone itself** (the `AMIX:` commits in the repository's
  history):
  - big-endian hosts;
  - no threads and no `<wchar.h>`;
  - marking the screen rows each draw touches;
  - the integer mixer;
  - the `/dev/paula` path.

## What you need

- AMIX 2.1 on a **68060** (the build is for the 68060), with the
  [68040/68060 kernel port](https://github.com/asokero/amix-040-060-port). Its
  shared-memory fixes make MIT-SHM safe.
- X11R6.3 for AMIX ([x11r6.3-amix](https://github.com/isoriano1968/x11r6.3-amix))
  and an X server with 16-bit colour. On a Z3660, use the `Xzz9000` from
  [soft3d-z3660rtg-amix](https://github.com/isoriano1968/soft3d-z3660rtg-amix).
- For Paula sound: `/dev/paula` from paula-amix. Otherwise `/dev/audio`.

## Building

On a Linux host with [gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix)
and its AMIX sysroot, which must include X11R6.3's `xc/exports`:

```sh
export PATH=$HOME/opt/m68k-amix/bin:$PATH
SHM_LIBS=/path/to/shm-libs sh amix/build.sh       # -> amix/pt2-clone
```

`SHM_LIBS` names a directory with two static libraries for MIT-SHM:

- `libXext.a` with the XShm client functions. X11R6.3's shared libXext has
  none. Take the static `libXext.a` of an X11R6.3 build with MIT-SHM on
  (`#define AmixHasShm YES` in x11r6.3-amix).
- `libshmsys.a`, the shared-memory system calls, made from your own AMIX's C
  library:

  ```sh
  ar x /usr/ccs/lib/libc.a shmsys.o syscall.o && ar rc libshmsys.a shmsys.o syscall.o
  ```

## Installing and running

Copy `amix/pt2-clone` to `/opt/pt2/pt2-clone` and `amix/pt2.sh` to
`/opt/amix/bin/pt2`, then:

```sh
pt2 [module.mod]
```

These environment variables are available:

| Variable | |
|---|---|
| `PT2_SCALE` | Window size, 1–6 times 320×255 (default 2). |
| `PT2_PAULA=0` | Mix and play through `/dev/audio` even when `/dev/paula` is there. |
| `PT2_HQMIX=1` | The float mixer instead of the integer one (`/dev/audio`). |
| `PT2_PAULA_PERIOD` | The mixing rate on `/dev/audio`, as a Paula period (default 128 = 27,710 Hz). |
| `PT2_OVERSAMPLE=1` | Oversampled mixing. |
| `PT2_NOSHM=1` | No MIT-SHM. |
| `PT2_NULLAUDIO=1` | No sound device at all; the replayer still runs in real time. |
| `PT2_FPS=1` | Frame rate and where the time goes, printed once a second. |
| `PT2_DIRTYDEBUG=1`, `PT2_EVDEBUG=1` | Debugging output for screen updates and events. |

pt2-clone ignores SIGTERM. Quit it from its own menu, or with `kill -9`.

## Testing on a PC

`amix/build-host.sh` builds the same program, with sdl2-amix, for Linux. That
lets you test the SDL layer in an `Xephyr` window:

```sh
Xephyr :5 -screen 1280x800x16 &
sh amix/build-host.sh && DISPLAY=:5 PT2_NULLAUDIO=1 ./amix/pt2-clone-host
```

## Licence

pt2-clone is under the BSD 3-Clause licence (`LICENSE`, Olav Sørensen). The
AMIX additions are under the same licence.
