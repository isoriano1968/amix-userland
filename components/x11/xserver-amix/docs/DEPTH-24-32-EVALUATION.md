# Xzz9000 at 24/32-bit depth: evaluation

2026-11-07. Question: should the AMIX X server (Xzz9000, X11R6.3, A3000 + Z3660
RTG) run at 24-bit colour in 32 bpp instead of today's 16-bit RGB565, and if
so, how?

**Short answer: no. On the Z3660 a 32 bpp framebuffer leaves the board as
RGB565 all the same.** The HDMI transmitter is wired with a 16-bit data bus,
and the FPGA's video formatter throws away the low bits of every 32-bit pixel
before they reach it. A 32 bpp desktop would cost twice the memory and twice
the bandwidth for every pixel the 060 or the ARM moves, and the screen would
show exactly the same colours. Stay at 16 bit. If the aim is less banding,
dithering into RGB565 is the cheap fix (section 6). Real deeper colour needs
an FPGA bitstream change, and that work is outside AMIX.

Nothing in this report was measured on the box. The sources settle the
question, the box was busy with another session's memory work, and a
measurement would not change the result. The cost figures in section 4 are
worked out from the A3000 numbers already measured (memory notes and
`bench/`). Section 7 has an optional test on the hardware, which would need
the user's OK to stop X.

---

## 1. What the Z3660 hardware and firmware offer

Sources: firmware clone `~/Sources/usr-amix/upstream/Z3660-amix` (HEAD
51038f3). The lines quoted are identical in `Z3660-beta21`, in `Z3660`
(shanshe) and in commit 16165fa ("fork 1.0", the firmware on the card).

### 1.1 Framebuffer formats

`src/rtg/gfx.h` `enum color_formats`: `MNTVA_COLOR_8BIT` (0, CLUT),
`MNTVA_COLOR_16BIT565` (1), `MNTVA_COLOR_32BIT` (2), `MNTVA_COLOR_15BIT` (3).
There is no packed 24-bit (RGB888) mode. The AmigaOS P96 driver
(`z3660-drivers/rtg/gfx.c:534`) offers CLUT, RGB565, RGB555 and **BGRA** for
32 bit. `RTG_COLOR_FORMAT_BGR888` is commented out. Its RGBFB table maps ARGB,
ABGR, RGBA and BGRA all to `MNTVA_COLOR_32BIT`: the hardware has a single
32-bit layout.

- Byte order: the formatter takes 32-bit words as the little-endian ARM sees
  them (`0xAARRGGBB`), so seen from the 68k the bytes in memory are B, G, R, A.
  A big-endian long write from the 060 is therefore `0xBBGGRRAA`. The video
  overlay works the same way, as already found by vplay
  (memory note z3660-firmware-video: "write BGRA bytes").
- The AMIX kernel driver's `zz9k_fill_fbinfo` (amix-gate
  `usr/src/uts/amiga/io/zz9000/.../zz9000.c:551`) reports ARGB masks
  (red `0x00ff0000` …) for 32 bit. Seen from the 68k that is wrong; it has
  never been used. A 32 bpp server would need the masks red `0x0000ff00`,
  green `0x00ff0000`, blue `0xff000000`, or an LSBFirst image byte order
  (section 2.4).

### 1.2 Mode and format registers

- `REG_ZZ_MODE` (`rtg/rtg.c:1174`): bits 0-7 video mode, 8-11 colour mode,
  12-15 scale mode → `video_mode_init(mode, scale, colormode)`. The AMIX
  driver writes it in `zz9k_set_mode_hw` (`zz9000.c:1125`), and
  `ZZ9KIOC_SETMODE` accepts any colour mode whose frame fits `fb_size`
  (`zz9000.c:2281`). Setting 32 bpp from AMIX therefore works without kernel
  changes.
- `REG_ZZ_COLORMODE` (`rtg.c:1142`) is the *blitter's* colour mode (low
  nibble, with a mask byte in bits 8-15). The firmware maps 15 bit to the
  16-bit blitter code.
- `video_mode_init` (`video.c:1223`): the VDMA always reads 32-bit words;
  16 bpp divides the horizontal size by 2 (`hdiv *= 2`). The formatter gets
  `MNTVF_OP_COLORMODE`.
- Modes (`zz_video_modes.h`): 640x480 … 1920x1080 (60/50 Hz), 1920x1200,
  1600x1200, 2560x1440@30, custom.

### 1.3 Blitter per format

`rtg/gfx.c` has every operation for 8, 16 and 32 bit: fill_rect,
copy_rect(_nomask), template/pattern fill, draw_line, p2d, invert, and
acc_clear_buffer/acc_flip_to_fb. Each one switches on the colour format
(`SET_FG_PIXEL32` etc.). **The firmware's blitter can do 32 bpp.** The AMIX
kernel driver refuses it:

- `zz9k_can_blit()` (`zz9000.c:1071`) requires `ZZ9K_COLOR_16BIT565`, so
  ZZ9KIOC_BLITS (the X server's fills and copies) falls back to the 060.
- `ZZ9KIOC_FILLRECT` and `ZZ9KIOC_COPYRECT` return `EINVAL` when the mode is
  not 565 (`zz9000.c:2333, 2355`). Fill colours are cast to `unsigned short`.
- `ZZ9K_KMAP_FB` = 4 MB (`zz9000.c:138`): the kernel's noncacheable window
  over the framebuffer is sized for 1920x1080x16 (0x3f4800). 1920x1080x32
  needs 8.3 MB.
- VT switching (`zz9000vtinfo`, `zz9000.c:1839`) returns −1 unless the mode is
  2 bytes per pixel, and its console buffer (`ZZ9K_VT_SIZE` 4 MB) is also
  sized for 16 bit. At 32 bpp, Alt+F consoles would stop working.

### 1.4 Scanout path and the decisive limit

`vivado/z3660.srcs/sources_1/new/video_formatter.v`:

```verilog
output [15:0] hdmi_data,                                              // line 61
assign hdmi_data = {dvi_rgb[23:19],dvi_rgb[15:10],dvi_rgb[7:3]};      // line 164
```

The XDC (`constrs_1/imports/new/gpio_axi.xdc:200-215`) puts exactly
`hdmi_data[0..15]` on pins. `sii9022_init.c` sets `AVI_IN_FORMAT` = 0x00 (RGB
in) and `AVI_OUT_FORMAT` = 0x00. The Z-turn's SiI9022A therefore gets
**5-6-5 RGB on a 16-bit bus** in every colour mode:

- 16 bpp: the formatter widens 565 to 888 by repeating bits
  (`red16 = {p[4:0], p[4:2]}` …, line 797), and line 164 then takes the top
  5/6/5 bits again. The result is the original 565 value.
- 32 bpp: line 164 takes the top 5/6/5 bits of the 8/8/8 value, with no
  dithering. The 2 or 3 low bits of each channel are dropped.
- The overlay plane (32-bit BGRA, `overlay_pix`) and the hardware sprite are
  merged into `dvi_rgb` *before* line 164, so they are cut to 565 as well.
  The core 1 video overlay shows 65536 colours today, even though its buffer
  is 32-bit.

**So 24/32-bit colour cannot be seen on this board.** A 32 bpp frame is
displayed with the same 65536 colours as RGB565, and the only difference is
rounding (truncation of 8-bit values instead of whatever the client's own
888→565 conversion did).

### 1.5 Scanout bandwidth (DDR3, shared with the 060's fast RAM)

All frames, the overlay and the 060's RAM (0x08000000-0x18000000) sit in the
same Zynq DDR3. Bytes per frame and VDMA read rate at 60 Hz:

| mode       | 16 bpp frame | 32 bpp frame | 16 bpp @60 | 32 bpp @60 |
|------------|-------------:|-------------:|-----------:|-----------:|
| 1920x1080  |      4.15 MB |      8.29 MB |   249 MB/s |   498 MB/s |
| 1280x1024  |      2.62 MB |      5.24 MB |   157 MB/s |   315 MB/s |
| 1280x720   |      1.84 MB |      3.69 MB |   111 MB/s |   221 MB/s |
| 1024x768   |      1.57 MB |      3.15 MB |    94 MB/s |   189 MB/s |
| 800x600    |      0.96 MB |      1.92 MB |    58 MB/s |   115 MB/s |
| 640x480    |      0.61 MB |      1.23 MB |    37 MB/s |    74 MB/s |

The overlay VDMA, when enabled, reads full 1920-pixel 32-bit lines
(`init_vdma_ovl(mode->hres…)`, another ~498 MB/s at 1080p60; the known fw
beta 21 overlay bug). At 1080p, 32 bpp plus the overlay is about 1 GB/s of
steady DDR reads, against the Z-turn's 32-bit DDR3-1066 (~4.2 GB/s peak, much
less in practice with mixed traffic). Every 060 access to fast RAM goes
through the same controller via core 0. The panel would keep up, but 32 bpp
takes DDR bandwidth from the 060, from core 1's rasterizer/H.264 work and
from the overlay, and buys nothing visible.

---

## 2. What the X server would need

Tree: `amix-userland/components/x11/xzz9000` (the old `xzz9000-z3` path in the
brief has moved; `src` → `private/xzz9000/src`). DDX:
`src/xc/programs/Xserver/hw/amix/rtg/` (`rtgInit.c`) and `rtg/zz9000/`
(3274 lines).

### 2.1 How the server draws today

- The DDX is **not** cfb. It is a custom DDX in the VA2000/XRTG style: every
  drawing op (`zz9000draw.c`, 1617 lines) writes straight to the framebuffer
  or to host-memory pixmaps through `drawBase()`. mfb takes only depth-1 GCs
  (`ZZ9000CreateGC`: `depth == 1` → `mfbCreateGC`, otherwise anything but 16
  is rejected). The libraries linked are `CFB16Libs` (Imakefile line 194), but
  `nm` of libzz9000/librtg/libamix shows only `mfb*` undefined, no `cfb*`
  symbol: cfb16 sits in the link and is not used.
- 16-bit throughout: `ZZ9000_BPP/DEPTH 16`, `ZZ9000_BYTES_PER_PIXEL 2`,
  `ZZ9000_PIXEL*` 565 macros and masks (`zz9000.h`); `fbBase` is
  `unsigned short *`; every span/fill/copy/pattern/PutImage/GetSpans loop in
  `zz9000draw.c` uses `unsigned short` pixels (about 60 places: fill runs that
  pack two pixels per long, `copyRun`, PatternSpans tiles, SetSpans,
  SolidRect, PaintWindow tile, CopyWindow/CopyArea, the soft3D swap);
  `ZZ9000CreatePixmap` takes depth 16 and 1 only; `blackPixel 0x0000`,
  `whitePixel 0xFFFF`; `rtgInit.c:511` builds one TrueColor visual with
  `ColormapEntries 64`, depth 16, pixmap format `{16,16,pad}` + `{1,1,pad}`.
- Fast paths: `local-putimage-move16.patch` (ZZ9000PutImage: 16-bit ZPixmap
  GXcopy straight into the drawable, move16 for the 16-byte-aligned middle,
  misaligned long reads when src and dst phase differ by 2 bytes); the card
  blitter through ZZ9KIOC_BLITS for fills and copies above `-blitmin`;
  `local-soft3d-swap.patch` (BLITX VRAM→VRAM copy of a soft3D back buffer).

### 2.2 What a 32 bpp (depth 24) build would change

1. `zz9000.h` / `zz9000hw.c`: a pixel type (`ZZPIXEL` = CARD16 or CARD32),
   BPP 32 / depth 24, masks and shifts as in 2.4, SETMODE with
   `ZZ9K_COLOR_32BIT`, `fbBase` typed accordingly.
2. `rtgInit.c`: visual depth 24, `bitsPerRGBValue 8`, `ColormapEntries 256`,
   pixmap formats `{24, 32, 32}` + `{1,1,32}`; `ZZ9000ScreenInit` root depth
   24, white `0x00ffffff` in the chosen layout; `ZZ9000ResolveColor` /
   `StoreColors` / `zz9000cmap.c` 888 rounding instead of 565.
3. `zz9000draw.c`: every loop retyped. Most loops get simpler (one pixel per
   long, so the 2-pixels-per-long head/tail cases disappear). The move16 path
   stays: the same 16-byte phase rule, now 4 pixels per burst; a 2-byte phase
   difference can no longer happen, so the misaligned-read branch becomes a
   0/4/8/12-byte case. Pattern/tile and PaintWindow tiles: 32-bit tile rows.
   Mechanical work, but every op needs retesting (CDE backdrops, Motif
   insensitive stipples, CopyWindow order fix).
4. `zz9000pix.c`: depth 24/bpp 32 pixmaps (host memory, twice the size).
5. Cursor (`zz9000cursor.c`) and the soft3D swap extension: BLITX at 4 bytes
   per pixel.
6. Kernel driver (amix-gate zz9000): `zz9k_can_blit` and FILLRECT/COPYRECT
   for 32 bit with a 32-bit fill colour, `ZZ9K_KMAP_FB` ≥ 8.3 MB, the VT
   console at 32 bpp (or a 16 bpp switch for the consoles), fbinfo masks
   corrected. That means a kernel relink and stepwise boot tests
   (one boundary per boot).

### 2.3 Is cfb32/mfb in R6.3 usable as is?

`programs/Xserver/cfb32` is in the tree (sources only, nothing built). cfb32
is the plain portable 32 bpp framebuffer code that XFree86 3.x used on
big-endian machines too. It would compile and work as an alternative DDX
(`cfb32ScreenInit` on the mapped framebuffer, as the stock `amixMono`/cfb
pattern does), but it would **lose** everything this DDX gained: the card
blitter, move16 PutImage, the soft3D swap, the CopyWindow and pattern fixes.
It also does every pixel through the 060 at 10.6 MB/s writethrough. A cfb32
server is a quick way to see a 32 bpp desktop (about a day to wire up), but
not one to use. mfb stays as it is for depth 1 in both cases.

### 2.4 Byte order choice

The 68k sees the 32-bit framebuffer pixel as `0xBBGGRRAA`. Two ways to
present it:

- **MSBFirst + odd masks** (red `0x0000ff00`, green `0x00ff0000`, blue
  `0xff000000`). Legal X; Xlib, Motif and CDE manage with any TrueColor masks.
  Mesa 3.1's xmesa recognises only the usual 8R8G8B / 8A8B8G8R layouts and
  would fall to the generic `PF_TRUECOLOR` path (per-pixel shifts, slow).
  TinyGL's nglx has only `MWPF_TRUECOLOR565`.
- **LSBFirst image byte order** with standard masks `0x00ff0000/0xff00/0xff`.
  Memory then matches. But every 68k client building a ZPixmap must swap
  bytes (Xlib does it in XPutPixel; Mesa's xmesa has byte-swapped variants),
  and a client-side swap costs about as much as the copy itself on the 060.

Either way, clients that compose pixels themselves are slower than today.

---

## 3. Clients and libraries that assume 16 bit

- **Mesa 3.1** (`components/x11/mesa-31/patches/0001-amix-port.patch`):
  `clear_16bit_ximage` uses `fill16` (move16 RAM fill, 512x320 in 1.7 ms) and
  the 5R6G5B span writers. A 32 bpp visual means the stock 8R8G8B path (or the
  slow generic one, see 2.4), a new move16 fill for 32-bit, and twice the
  back-buffer bytes.
- **TinyGL** (`components/library/tinygl/src/nglx.c:94`): `MWPF_TRUECOLOR565`
  only; its zbuffer writes 16-bit pixels.
- **soft3D on core 1** (`components/library/soft3d/libs3d/s3d.h:67-73`):
  colour buffer and `s3d_clear_color` are RGB565, and the back buffer lives in
  off-screen VRAM and is blitted by BLITX. A 32 bpp desktop needs a 32-bit
  render target in the core 1 rasterizer (and in the gpu core image, amix-gate
  `firmware/core1/cores/gpu`) or a 565→8888 widening in the swap. Twice the
  ARM write bandwidth per frame.
- **ffplay/vplay 060 path**: swscale to RGB565 + XPutImage/MIT-SHM. At 32 bpp
  each frame is twice the bytes. **Core 1 video path**: H.264 → BGRA overlay
  plane. It does not depend on the desktop depth and is already 32-bit, but
  is cut to 565 at the HDMI pins too (1.4).
- **MIT-SHM**: SHMMAX on the running kernel is 327680 unless the
  `unix.z3-shm` patch (16 MB, not the default) is in. A 512x320 frame is
  exactly 327680 bytes at 16 bpp and 655360 at 32 bpp, which fails `shmget`
  and drops to socket XPutImage.
- **CDE/Motif**: they work at any TrueColor depth. Pixmaps (backdrops, icons,
  dtwm decorations) live in server host memory and double in size. Nothing
  else changes.
- **Other clients that draw their own pixels** (pt2-clone through sdl2-amix,
  NetSurf's Xlib surface, Grafx2's 16-bit X11 patch, amixsysmon) would need
  their 16-bit assumptions checked one by one.

---

## 4. Costs on this machine (estimates from measured A3000 numbers)

Measured basis: single long stores to VRAM ~10.6 MB/s, move16 into VRAM
26 MB/s, byte-loop libc ~17 MB/s, 512x320 SHM put 14 ms (x%8==0), RAM fill
with move16 1.7 ms (MIT-SHM notes, `bench/vrambw.c`, `bench/shmput.c`).

| item | 16 bpp (today) | 32 bpp | note |
|---|---:|---:|---|
| 1080p framebuffer | 4.15 MB | 8.29 MB | VRAM has room (128 MB RTG region); kernel kmap/VT buffers must grow |
| full-screen repaint by the 060 (long stores) | ~0.39 s | ~0.78 s | window moves without the blitter, expose of large areas |
| 512x320 PutImage, move16 aligned | ~13-14 ms | ~25-28 ms | SHM swap for GL windows |
| 512x320 PutImage, long stores | ~31 ms | ~62 ms | misaligned windows |
| 512x320 back-buffer clear (move16) | 1.7 ms | ~3.4 ms | Mesa `fill16` equivalent |
| SHM segment 512x320 | 320 KB (= SHMMAX) | 640 KB (> stock SHMMAX) | falls back to socket put |
| scanout @1080p60 | 249 MB/s | 498 MB/s | DDR shared with the 060 RAM, core 1, overlay |

Expected frame rates (only the byte-bound parts scaled; everything else held
fixed):

- **Mesa gears on the 060**, 10.8 fps (~93 ms): clear +1.7 ms, swap +12-14 ms,
  span writes up to +10 ms → **about 8-9 fps**. Worse if xmesa ends up on the
  generic `PF_TRUECOLOR` path.
- **LamerCubeAMIX on the 060**, 7.5 fps (~133 ms) → **about 6-6.5 fps**.
- **Intro (soft3D)**, 32.6 fps: core 1 writes twice the colour bytes and the
  BLITX swap moves twice the bytes. With the 32-bit render target added, an
  estimated **10-25 % slower (~25-29 fps)**. Without one there is no soft3D
  at 32 bpp at all.
- **ffplay 060 path**: the YUV→RGB conversion writes twice the bytes and the
  put doubles. At 512-wide clips it is already CPU-bound, so expect a
  **20-40 % lower** frame budget. The core 1 overlay path is unchanged.
- **Desktop (CDE)**: window moves and exposes get slower wherever the blitter
  is not used (small boxes, pattern fills, PutImage of icons).

---

## 5. Options

| option | visible gain | effort | cost |
|---|---|---|---|
| A. 32 bpp everywhere | **none** (HDMI is 565) | server ~3-5 days, kernel driver ~2 days + boot steps, soft3D/core 1 32-bit target ~1 week, Mesa/TinyGL/clients days | 2x memory/bandwidth; slower GL, video, desktop; VT consoles break until fixed |
| B. 32 bpp server, 16-bit fast paths kept (depth-16 visual alongside a depth-24 root, or a runtime choice) | none | more than A: two pixel paths in every op, multi-depth GCs/pixmaps, CopyArea between depths | the same costs when 24 is in use, and more code to maintain |
| C. Per-resolution choice (e.g. 32 bpp at ≤1024x768) | none | as A plus the selection | as A at those resolutions |
| D. cfb32 quick server | none | ~1 day | loses blitter/move16/soft3D: slowest desktop |
| **E. Stay at 16 bit** | – | 0 | – |

## 6. Recommendation

**Stay at 16-bit RGB565 (option E).** On the Z3660, as built today, 24-bit
colour cannot reach the screen: the FPGA sends `{R[7:3], G[7:2], B[7:3]}` over
16 HDMI data pins in every mode, overlay and sprite included. Every option
from A to D pays twice the bytes on a machine whose 060 writes 10-26 MB/s,
and gives back nothing a viewer could see.

If the real complaint is banding (gradients in CDE backdrops, video, GL
shading), these are the cheap fixes, all within 565:

1. **Dither into 565 in the clients.** swscale already does ordered dither for
   RGB565 output (check that the AMIX ffplay build does not set `-sws_dither
   none` or `SWS_POINT` paths that skip it). Mesa's xmesa has dithered 16-bit
   formats in later versions; check whether 3.1 has `PF_DITHER_5R6G5B` and
   enable it for GL clients. soft3D/TinyGL could do a 4x4 ordered dither in
   the rasterizer's final 565 pack on core 1, which is nearly free on NEON.
2. **Small cleanup with no depth change:** correct the 32-bit masks in
   `zz9k_fill_fbinfo` (BGRA seen from the 68k), so that a future user of
   SETMODE 32 does not trust ARGB.

Real deeper colour would only come from an FPGA change. The SiI9022A also
takes YCbCr 4:2:2 on a 16-bit bus (8-bit Y + alternating 8-bit Cb/Cr), which
gives full 8-bit luma. Temporal or ordered dithering inside the formatter from
a 32 bpp framebuffer is another way. Both mean editing `video_formatter.v`,
rebuilding the bitstream in Vivado (not on the P52), changing
`AVI_IN_FORMAT`, and only then the server work of option A. That is a
firmware project for upstream (shanshe/Z3660), not AMIX work, and the 2x
bandwidth cost on the AMIX side would still apply.

## 7. Optional confirmation on the hardware (needs the user's OK)

To see the truncation directly: with X stopped (ask first; through
`amix-box`, never while a player runs), a small test program would call
`ZZ9KIOC_SETMODE` 1280x720 `ZZ9K_COLOR_32BIT` and draw a 256-step grey ramp
as `0xVVVVVV00` longs, then the same ramp in RGB565 at 16 bpp. Both should
show the same 32 steps in red and blue and 64 in green. Then restore the mode
(or restart X). This proves 1.4 on the real card but is not needed for the
decision.
