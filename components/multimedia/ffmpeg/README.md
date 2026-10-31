# ffmpeg-amix

FFmpeg 4.4.8 with ffmpeg and ffplay on Commodore Amiga UNIX (AMIX, SVR4, m68k),
and `vplay`, a small player, cross-built for an A3000 with a Z3660 (68060).
FFmpeg itself is not here: `make compat` fetches and unpacks the release.

On a Z3660 with core 1's player loaded, H.264 and AAC are decoded by the
board's second ARM core: picture in the RTG overlay, sound on HDMI. The 060
reads the file or the network and hands the packets over.

- `configure-amix.sh`: the configuration. HTTP and TCP are on.
- `compat/`: what AMIX lacks: 68060 FPU rounding (its libm has no `lrint`),
  `gettimeofday` without a system call, fast `mem*`, socket prototypes,
  a blocking `connect` (AMIX's `poll` reports a non-blocking one done too early).
- `patches/`: ffplay: refresh rate, core 1 (below).
- `src/zzav.c`: the link to core 1, used by vplay and compiled into ffplay.
- `src/vplay.c`: the player, on X11 with MIT-SHM. `src/zzmpeg.c`: MPEG-1 by the firmware.
- `tools/`: launchers for the box.

## ffplay on core 1

Left/Right 10 s, Down/Up 60 s, PgUp/PgDn 10 min, right click a place in the
file, p pause, m mute, q quit. 1, 2, h: normal, double and half size; f
fullscreen. The window's title shows position and length.

A seek opens core 1's player anew at the new place. For a stream of
[AmixCast](https://github.com/isoriano1968/amix-userland/tree/main/components/desktop/cde-apps) the PC starts a new
stream there; its URL carries the length (`&d=`) and the start (`&t=`). A
stream waits for 3 s of sound before it starts.

## Building

With gcc14-cross-amix (`m68k-elf-gcc`) in `~/opt/m68k-amix/bin`:

    make build                 # ffmpeg and the libraries
    SHM_LIBS=<dir> make vplay  # vplay
    make ffplay                # ffplay

`ffplay` needs `components/library/sdl2` (`SDL2_AMIX`) and `components/library/pth`
(`PTH_LIB`); `SHM_LIBS` names a directory with `libXext.a` (with XShm) and
`libshmsys.a`. `mkclips.sh` makes test clips with GStreamer.
