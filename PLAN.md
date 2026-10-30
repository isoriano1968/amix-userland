# vplay-amix: a video player for AMIX

Goal: play MP4/H.264 (and simpler formats) on AMIX, on an A3000 with a
Z3660 (68060 at 80 MHz), and be ready to hand decoding to faster hardware
later without rewriting the player.

## Design

The player owns the file, the A/V sync and the window. Decoding and display
sit behind one small backend interface:

1. **CPU:** FFmpeg 4.4 (libavcodec) on the 68060. Works today, for small
   sizes.
2. **Z3660 ARM:** the 68060 passes compressed data to the board's ARM, which
   decodes into VRAM. Needs an addition to the (open source) Z3660 firmware,
   so it is the owner's decision to flash it. A smaller first step uses the
   firmware as it is: colour conversion and scaling on the ARM or the
   blitter, e.g. a frame drawn as a soft3D texture.
3. **FPGA + CM4 card:** the 68060 forwards the stream; the CM4's hardware
   decoder does the work.

## Phase 0: results (2026-10-28)

FFmpeg 4.4.8 cross-built with gcc14-cross-amix (`configure-amix.sh`), C only,
no threads. Synthetic clips from `mkclips.sh` (animated zone plate: fine
detail and constant motion, so harder than most film), 10 s, 25 fps.
Decode only (`-f null`), on a quiet machine.

**Video:**

| Clip | Decode | fps |
|---|---|---|
| H.264 baseline 160×90 | 6.8 s | 37.0 |
| H.264 baseline 320×180 | 21.3 s | 11.7 |
| H.264 main 320×180 | 31.3 s | 8.0 |
| H.264 baseline 480×270 | 42.8 s | 5.8 |
| MPEG-4 Part 2 320×180 | 13.9 s | 18.1 |
| MPEG-4 Part 2 320×180, `-lowres 1` (160×90 out) | 9.9 s | 25.3 |
| MPEG-1 320×180 | 12.0 s | 20.9 |

- H.264 `-skip_loop_filter all -flags2 fast`: only ~5 % faster. The time
  is elsewhere; profile before optimising.

**Audio** (10 s; FFmpeg start-up of ~0.95 s subtracted):

| Audio | Decode | Share of real time |
|---|---|---|
| AAC 44.1 kHz stereo | ~6.8 s | ~68 % |
| MP3 44.1/32 kHz stereo, fixed-point decoder | 27.6 s | 276 % (unusable) |
| MP3 stereo, float decoder (`mp3float`) | ~6.6 s | ~66 % |
| MP3 22 kHz mono (float) | ~3.4 s | ~34 % |
| MP2 22 kHz mono (float) | ~2.4 s | ~24 % |
| AAC 22 kHz mono | ~2.3 s | ~23 % |

- The fixed-point MP3 decoder uses 64-bit multiplies; gcc implements them
  with `__muldi3` calls on the 68060. Use the float decoders.
- Uncompressed 8-bit PCM at 22 kHz mono costs ~nothing and is 22 KB/s.

**Colour conversion:** FFmpeg's swscale YUV420 → RGB565 adds ~22 ms per
320×180 frame. A 68060-specific converter is part of Phase 1.

**What this means:** with audio and display included, expect roughly
- MPEG-1 / MPEG-4 Part 2 at 320×180: ~12–15 fps;
- H.264 baseline at 320×180: ~7–8 fps;
- H.264 at 160×90: 25 fps.
To be confirmed by the player itself.

## Porting notes

- AMIX lacks `<inttypes.h>`, `strtoll`, `strtoull`, `llabs`, `scalbn`,
  `MAP_FAILED`: `compat/` (`libvplaycompat.a`, `amix-ff.h` forced in).
- gcc's `__STDC__` is 1, so AMIX headers hide their extensions unless
  `_XOPEN_SOURCE` or `_POSIX_SOURCE` is defined. `_XOPEN_SOURCE` hides
  `memalign` in stdlib.h; `sigset_t` needs `_POSIX_SOURCE`.
- **AMIX's `memalign` blocks break `realloc`/`free`** (heap corruption,
  SIGSEGV in libc's `realfree`). `HAVE_MEMALIGN` is forced to 0; the 68060
  build needs no SIMD alignment.
- `-Wno-error=incompatible-pointer-types`: `int32_t` is `long` here.
- The `ffmpeg` tool puts the terminal in raw 8-bit mode for its `q` key and
  can leave it that way: always run it with `-nostdin`.
- The banner's configuration line contains the build paths; strip them
  before anything is published.

## Phase 1: the player (2026-10-28, working)

`src/vplay.c`, built by `build.sh` (AMIX) and `build-host.sh` (Linux, for
Xephyr); installed as `/opt/vplay/vplay` with the launcher
`/opt/amix/bin/vplay` (`vplay.sh`). `mkamixvideo.sh` converts anything into
what plays well.

- One loop, no threads. Wall clock anchored at the first frame (after the
  sound buffer is filled, with a 0.2 s head start); sound written by its
  timestamps (late samples dropped, gaps filled with silence); late frames
  dropped; decoder skips non-reference frames / up to the next keyframe
  when it keeps falling behind.
- Sound: `/dev/audio` with `O_NDELAY`, signed 8-bit, Paula period nearest
  the source rate; no resampling within 0.5 % (resampling 22050 -> 22030
  cost 15-30 % of the CPU).
- Picture: own YUV420 -> RGB565 tables, optional 2x. On the Z3660 with
  ZZ9K-SOFT3D: converted straight into off-screen VRAM (two buffers from
  row 2208) and blitted by the card through the window's clip list.
  Otherwise MIT-SHM in bands (SHMMAX 320 KB).
- `-stats` prints fps, drops, lag and where the CPU goes.

Measured on the A3000 (320x180, MPEG-4 + MP2 22 kHz mono, 12.5 fps, as
`mkamixvideo.sh` makes it): 12.2-12.9 fps shown, lag 13-80 ms, CPU ~93 %:
video decode 40-46 %, conversion ~19 %, blit ~8 %, sound 15-19 %, file 2-8 %.
The same clip at 25 fps shows 9-11 fps; the zone plate (worst case) ~10 fps.

- Stores are the limit of the conversion: ~10.6 MB/s for plain long stores,
  to VRAM and to (write-through) RAM alike; VRAM is no faster than SHM for
  the conversion, but the blit removes the server's copy (put 15-20 % ->
  7-10 %).

Next:
- CDE: a file type and action so .avi/.mp4 open in vplay.
- Profile the MPEG-4 and H.264 decoders on the 68060.
- Conversion with move16 from a cache-resident row (if user RAM allows a
  copyback buffer), or on the Z3660's ARM (Phase 2a).

## Phase 1 plan (as written before the work)

- `vplay` on libavformat/libavcodec/libswresample, own output:
  - YUV420 → RGB565 converter for the 68060, optional 2× scale in the same
    pass, into an MIT-SHM image (direct framebuffer later);
  - audio to `/dev/audio` (8-bit), audio clock drives sync, late frames
    dropped; decoder speed switches on demand;
  - keys, `-stats` (fps, drops, time per stage), CDE launcher.
- `mkamixvideo.sh` on the PC: transcode anything to what plays well
  (likely MPEG-4 Part 2 or H.264 baseline ~320×180, 8-bit PCM or MP2
  22 kHz mono).
- Profile H.264 with the 060 profiling tools before optimising.
