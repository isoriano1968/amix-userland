# lib-mikmod-amix

MikMod for **AMIX**, Commodore's Amiga UNIX. It plays MOD, S3M, XM, IT and
the other tracker formats MikMod knows. You get the library and the
full-screen player:

| | Version | |
|---|---|---|
| `libmikmod/` | libmikmod 3.1.23 | The library, with two AMIX output drivers. |
| `mikmod/` | mikmod 3.2.9 | The player (curses interface, playlists). |

## Two ways to make sound

**Through Paula itself (`paula`).** On a 68040 or 68060 AMIX kernel with the
[paula-amix](https://github.com/isoriano1968/paula-amix) driver, libmikmod
drives Paula's four channels the way an Amiga tracker does:
- Each sample is copied to chip RAM once, and the kernel replays the
  register changes at the song's tempo.
- The CPU only runs the player and mixes nothing.
- The left and right channels sit where an Amiga puts them.
- Up to four channels.

**Mixed by the CPU (`amix`).** The driver for AMIX's own `/dev/audio`. It
works on every AMIX machine, including a stock A3000 with a 68030:
- libmikmod mixes any number of channels in software, and the driver plays
  the result.
- Stereo needs an audio driver with locked stereo (`AUDIO_STEREO`), which no
  published kernel has yet. Otherwise the driver plays mono, or stereo through
  two opens (see `AMIX_AUDIO_DUALOPEN` below).

**Which one plays.** libmikmod tries `paula` first and falls back to `amix`
when `/dev/paula` is not there or is in use.

The player also switches by itself between modules:
- A module with more than four channels goes to `amix`.
- The next module that fits goes back to Paula.

Measured on an A3000 with a 68060: a CPU-bound program running beside mikmod
lost about 11 % of its speed with `amix` and about 5 % with `paula`.

## What you need

- AMIX 2.1.
- `gcc` on the AMIX machine. Both Makefiles call `gcc -m68020`, so the
  binaries run on a 68030, 68040 or 68060.
- `ncurses` (`curses.h` and `libncurses.a`) for the player.
- For the `paula` driver: a 68040/68060 kernel with `/dev/paula`, see
  paula-amix.

## Building and installing

On the AMIX machine, with the repository copied there:

```sh
cd libmikmod
make
make install        # as root: /usr/local/lib/libmikmod.a, /usr/local/include/mikmod.h
cd ../mikmod
make
make install        # as root: /usr/local/bin/mikmod, /usr/local/share/mikmod/mikmodrc
```

The upstream `configure` scripts are left in place but not used. autoconf and
libtool do not run on AMIX, so each directory has a plain SVR4 `Makefile`
instead.

## Playing

```sh
mikmod song.mod
mikmod -d amix song.xm          # force the mixing driver
mikmod -d paula song.mod        # force Paula (four channels at most)
```

These environment variables tune the `amix` driver:

| Variable | Effect |
|---|---|
| `AMIX_AUDIO_GAIN` | Output level, 0.1 to 8 (default 1). Many modules sound quiet on Paula without it; 2 or 3 is a good start. |
| `AMIX_AUDIO_MONO` | Mono output, even where stereo is available. |
| `AMIX_AUDIO_DUALOPEN` | On kernels without locked stereo: stereo by opening `/dev/audio` twice, one channel per side. The two sides can drift apart. |
| `AMIX_AUDIO_DEBUG` | Prints the driver's settings when it starts. |

These help with diagnosis:

| Variable | Effect |
|---|---|
| `MIKMOD_AMIX_DEBUG=/tmp/mikmod.log` | Writes which driver plays each module to that file, and why `paula` was passed over. The player's screen hides normal error output. |

## Using libmikmod in your own programs

Link with `-lmikmod`; the library's API is the standard MikMod one. A program
gets the same driver choice as the player: `paula` first, then `amix`.
`libmikmod/examples/simpleplay` is a small example player, ported to AMIX
with its own Makefile.

## What was changed for AMIX

**libmikmod**
- `drivers/drv_paula.c`: the Paula driver. It is a hardware-voice driver,
  like libmikmod's Gravis Ultrasound driver.
- `drivers/drv_amix.c`: the `/dev/audio` driver. It converts to the signed
  samples Paula plays, and handles locked stereo, the mono fallback and the
  output gain.
- `include/amix_paula.h`: the `/dev/paula` interface. It's a copy of
  paula-amix's `paula.h`.
- `playercode/mdreg.c`: registers both drivers, `paula` first.
- `Makefile` and `config.h`: the hand-written build for AMIX.
- `examples/simpleplay`: the example builds on AMIX.

**mikmod**
- `src/mikmod.c`: the switch between `paula` and `amix` for modules with more
  than four channels, and the `MIKMOD_AMIX_DEBUG` log.
- `src/amix_strcasecmp.c`: `strcasecmp`, which AMIX's libc lacks.
- `Makefile` and `config.h`: the hand-written build for AMIX. It uses
  upstream's own fallbacks for `fnmatch`, `getopt_long` and `usleep`.

## Licences

- libmikmod is under the GNU LGPL 2 or later (`libmikmod/COPYING.LIB`).
- mikmod is under the GNU GPL 2 or later (`mikmod/COPYING`).
- The AMIX changes are under the same licence as the files they are in.

## Credits

- The MikMod authors (see `AUTHORS` in each directory).
- asokero, for the AMIX 68040/68060 kernel port that `/dev/paula` builds on.
