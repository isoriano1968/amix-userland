# Quake III on the Z3660's ARM core 1

The whole of ioquake3 (client, server, botlib, the game modules, sound
mixing) and its GL (Mesa 18.3 OSMesa with softpipe) as one bare-metal program
on the Z3660's second Cortex-A9, `z3660ARM_Core_q3.bin`, the way the Amiga
emulator runs there (`components/emulators/uae4arm`). The 68060 only holds
the X window the picture lies over, passes keyboard and mouse on, and serves
the files: `c1q3`.

```
 AMIX (68060)                                   Z3660 core 1 (Cortex-A9, bare metal)
 ----------------------------------             ---------------------------------------
 c1q3 (setuid root, drops root early)           z3660ARM_Core_q3.bin
   X window (black, under the overlay)            core1_main.c  control page, MMU, faults, reload,
   keys/mouse -> input ring                                     overlay, 200 Hz timer: HDMI pump + sampler
   | /dev/zz9000: board memory + registers        sys_c1.c      Sys_*, the game modules linked in
   |   C1Q3 page 0x07AFD000 (commands, input, status)           net_c1.c  loopback only, NET_Sleep polls the 68k
   |   window   0x07100000 (files in/out, log ring at its end)  c1fs.c    files in memory, written ones go back
   |   overlay  0x06000000/0x06800000 <-- picture ---          glimp_c1.c OSMesa, frame -> overlay
   |   HDMI     0x07CE0000  <--------------- sound ---          snd_c1.c  48 kHz ring the timer drains
   v                                                            in_c1.c   input ring -> engine events
 armbench -load/-reload (amix-gate firmware/core1)
```

Why: on the A3000 (060 at 80 MHz) with the GL on core 1 through zzgl and the
engine on the 060, q3dm1 ran at ~3 fps at 640x480; a /proc profile showed the
060's own share would cap it even with a fast GPU (RtCW ~1.5 fps). An A9 at
866 MHz runs this kind of code several times faster than the 060, and nothing
has to cross the bus per frame.

## Choices

**Same harness as uae4arm, own protocol.** `core1_main.c` is uae's harness
(control block, MMU table, fault handler, `-reload` trampoline, private-timer
sampler) with the protocol in `c1q3_board.h`: the same areas (the player's
control page, its packet ring as the data window, the overlay buffers, the
HDMI ring), magic "C1Q3". The image runs alone on core 1; while the game runs
there is no zzgl, H.264 or crypto offload for others (sshd falls back to the
060).

**Game modules linked in.** No dlopen on bare metal: cgame, qagame and ui are
each compiled with their own define, linked into one relocatable object
(`module.ld`) with only `dllEntry`/`vmMain` left global (renamed
`<mod>_dllEntry`/`<mod>_vmMain`), so their copies of `q_shared.c`, `bg_*.c`
and the `trap_*` functions never meet. `module.ld` gathers each module's
globals between `<mod>_data_start/_end` and `<mod>_bss_start/_end`;
`Sys_LoadGameDll` gives them back their first values at every load, as a
fresh dlopen would (cgame and qagame are loaded again at every map). FS_FindVM
wants a file per module: c1fs gets a one-byte `/base/<game>/<mod>.so` for
each game directory. No QVMs and no QVM JIT (its cache flush would be a Linux
system call, the trap uae4arm fell into).

**Platform files instead of SDL.** `sys_c1.c` replaces `sys_main.c`,
`sys_unix.c` and the consoles; `net_c1.c` replaces `net_ip.c` (no sockets:
the local client and server talk over the loopback in `net_chan.c`);
`glimp_c1.c`, `in_c1.c` and `snd_c1.c` replace `sdl/`. The engine sources are
pristine plus `patches/0004-amix-core1.patch` (the `__z3660core__` platform
block in `q_platform.h`, hunk 64 MB / zone 24 MB defaults, one raw sound
stream instead of 129, which were 16.5 MB of .bss).

**GL: OSMesa straight.** The renderer (renderergl1, linked in) gets its GL
functions from `OSMesaGetProcAddress` and draws into an RGBA buffer in core
1's memory (depth 16, no stencil, as the zzgl build). `GLimp_EndFrame`
converts it into the overlay's back buffer (0x00RRGGBB, 0 is transparent so
it becomes 0x010101) and asks core 0 to flip at vblank. The library is
amix-gate's `build/arm/libOSMesa.a` (`build/xbuild.py arm`), the same one the
gpu core uses, with its fast softpipe paths. Later the renderer can move to
RRXGL (RasterIX); not part of this image.

**Sound: the engine's ring, drained by a timer.** The HDMI ring is 160 ms; a
frame of the game is longer than that. The engine mixes 48 kHz stereo into
its own 64K-frame ring (`s_mixahead 0.5`, set by c1q3); the private timer
interrupt (200 Hz) copies what is painted into the HDMI ring 80 ms ahead of
the DMA and writes silence where nothing is painted yet, so a slow frame
gives a gap, not a loop of old sound. The interrupt saves the caller-saved
VFP/NEON registers. The engine's clock (`SNDDMA_GetDMAPos`) is the DMA's
position (or core 1's clock while the DMA stands, as in qemu), moved on when
the engine slept longer than half its ring so it never counts wraps wrong.

**Memory.** The image is ~9 MB (limit 12 MB); .bss ~16 MB, stack 4 MB. The
heap is 0x30000000-0x38000000 (128 MB) and 96 MB above the image. newlib's
malloc takes no memory below its old top, so `_sbrk` gives the low region
first and moves up once; the paks are cut from the top of the high region
(`c1p_file_alloc`) before the engine starts. The demo: pak 46 MB, hunk 64,
zone 24, sound 25, leaves ~42 MB for textures and Mesa after q3dm1 loads.

**Files.** c1q3 hands over every file of `<base>/<game>` (as `/base/<game>/...`,
read-only) and of the user's home (default `~/.q3a`, as `/home/...`). Only
`/home/` is writable on core 1; written files come back whole through the
window (`CMD_FLUSH`, in pieces of up to 5.9 MB), into `NAME.part` and renamed,
removals too. c1q3 refuses paths outside `/home/`.

**Input.** c1q3 maps X keysyms to the engine's key numbers (as `amix_input.c`
does in the 060 build), sends printable characters as CHAR events, mouse
buttons and the wheel as keys, and motion relative (pointer grabbed, hidden
and warped to the centre while the game wants the mouse: core 1 says so in
`W_MOUSE`, not in the console and not while a map loads). F10 quits (c1q3
binds it), as does closing the window.

## Building and testing

```
make prep              # pristine ioquake3 + patches/*.patch
make core1             # build/core1/z3660ARM_Core_q3.bin (+ q3.elf, q3.map), build/amix/c1q3
make check-core1       # the real image in qemu's Zynq, fake060.py as the 68k: q3dm1 loads,
                       # is drawn, quit writes q3config.cfg back (Q3DATA= the demo's data)
make stage-core1       # what goes on AMIX; files/amix/install-c1q3.sh puts it there
```

`files/qemu/run.sh` runs `build/core1/q3.elf` in `qemu-system-arm -M
xilinx-zynq-a9` (Debian's qemu in ~/opt/qemu), its DDR a shared file, and
`fake060.py` does c1q3's part (files, command line, console commands, keys,
overlay dumps as PPM, write-back). qemu has no core 0, so the harness turns the
GIC distributor on itself when it finds it off (on the board core 0 has).
qemu's timing says nothing about the board's.

PC results, 2026-11-08 (qemu): the UI comes up (the CD-key screen, as the
060 build at its first start: ioquake3's q3_ui asks for a well-formed key
unless one is stored); `devmap q3dm1` loads all three modules, the bots' AAS
and the map (CL_InitCGame 17 s in qemu), draws it right (lightmaps, sky,
models, HUD), the shotgun's sound reaches the HDMI ring; `quit` writes
`q3config.cfg` and `games.log` back. com_speeds in qemu: server, client and
game ~5 ms a frame, the rest is softpipe.

Board, 2026-11-08 (A3000, core 1 at 866 MHz, 640x480, full quality as q3.sh):

| | zzgl build (engine on the 060) | c1q3 (all on core 1) |
|---|---|---|
| files into core 1 | - | 44.7 MB in 6-36 s (disk cache) |
| CL_InitCGame | 77.7-80.2 s | **6.6 s** |
| q3dm1 spawn view, a frame | 225 ms (4.4 fps), 2026-11-07 | **221 ms (4.5 fps)** |
| of it: engine (sv+cl+gm+rf+ev) | ~45 ms on the 060 | **~4 ms** |
| of it: softpipe (bk) | ~225 ms on core 1 | 217 ms |

The engine is no longer a cost; softpipe sets the frame rate, as it did with
zzgl. Loading is 12 times faster. So the next gains are all in the renderer:
the sp_fast paths, fewer pixels, or RasterIX (with it the 4 ms engine would
allow far more than the 060 ever could). Quit wrote q3config.cfg back; no
sound underruns counted.

Traps on the board: the 060 build's `q3config.cfg` holds `com_hunkmegs 256`
and `com_soundMegs 16` (50 MB): c1q3 now sets hunk 64, zone 24, sound 8 ahead
of the user's arguments. A 50 MB request also cost ~38 MB of the low region
(malloc only grows upward): a better allocator over both regions is to do.
Root has no X cookie for the user's display: run c1q3 as the user, or with
XAUTHORITY of the session.

Later the same day:

- **Allocator:** dlmalloc 2.8.6 (`library/dlmalloc`, non-contiguous MORECORE over
  both heap regions, `c1_malloc.c` maps newlib's `_malloc_r` family onto it) in
  place of newlib's 2.6, which takes no memory below its top. The 060
  config's 50 MB of sound loads again; and frames got faster: spawn view
  **159 ms (6.3 fps)**, 221 before, other views near 10 fps (the user).
  Trap: GCC turns dlmalloc's calloc (malloc + memset) into a call to calloc,
  itself: build it with `-fno-builtin`.
- **Files by request** (protocol 0.1.2): files over `-remote-over` MB (64)
  stay on AMIX; core 1 asks for 256 KB blocks through the window's REQ area
  and keeps 48 MB of them (LRU). With the demo pak so: start 0.4 s instead
  of 6-17 s, CL_InitCGame 21.5 s instead of 6.3 s (each block waits for the
  060's disk). For RtCW, whose paks do not fit.
- **Profile** (`c1q3 -profile`, every 200 Hz tick's pc; `files/tools/c1prof.py`
  with the ELF the stage kept in `build/elf/`): ~70 % softpipe's span
  routines, `nspan_1_2_0` (texture x lightmap, depth test, no blend) 45 %,
  `nspan_1_1_*` 25 %, tex_setup 5 %, vertex shader 3 %, present 2 %; the
  engine itself ~3 %. A second spawn-view run gave 226 ms with the same
  shape: scripted views vary; compare with a timedemo.

## RtCW (games/iortcw), the same image design

`games/iortcw/files/core1/Makefile` builds `z3660ARM_Core_wolf.bin` from
iortcw's SP engine, renderer and modules with this directory's harness and
glue (`-DC1_WOLF`: `Sys_GetDLLName` names, iortcw's few own system calls,
the renderer's header; `-DC1_CORE_NAME="wolf"`). Its module lists come from
upstream's Makefile (`lists.py`, as `amix/build.py`). `patches/0004` there:
the platform block, hunk 64 / zone 16, 16 raw sound streams (RtCW streams
its dialogue; 257 were 33 MB), and the pak checks off as for the 060 build
(the paks are the user's own, reduced by trimpaks.py). The image is 9.9 MB,
.bss 40 MB, heap 128 + 72 MB, the block cache 24 MB. c1q3 runs it
(`-core .../z3660ARM_Core_wolf.bin -base /opt/rtcw -game main`, which a user
may name too); `wolf.sh` does that with RtCW's settings and `+spmap escape1`;
the 198 MB pak stays on AMIX (by request), the 21 MB one goes in whole.

Board, 2026-11-09 00:00 (escape1, 640x480, com_recommendedSet 1, fresh home;
the user clicked past the briefing). Verified interactively afterwards: the
user played escape1 with mouse and keyboard, "noticeably faster":

| | iortcw, engine on the 060 + zzgl | wolf image, all on core 1 |
|---|---|---|
| files into core 1 | - | pak1 21 MB in, pak0 198 MB by request (13 s) |
| CL_InitCGame (escape1) | ~200 s (534 s to the end of loading) | **46.7 s** |
| a frame in the level | ~900 ms (1.1 fps) | **~420 ms (2.4 fps)**, 365-500 |
| of it: engine (gm+cl+ev+sv+rf) | ~600 ms on the 060 | ~21 ms |
| of it: softpipe (bk) | the rest, on core 1 | ~344 ms |

Profile (250 s in the level): span routines ~60 % (`nspan_1_2_0` 27 %,
`nspan_1_1_*` 30 %), `util_format_r8g8b8a8_unorm_pack_rgba_float` 6 % (a
generic path softpipe takes for some of RtCW's draws: a target), tex_setup
5 %, vertex shader 3.5 %, c1p_fetch 1.4 % (pak blocks during play), present
1.2 %.

## Speed work: how it is measured

`files/tools/boxbench.sh q3|wolf LABEL` runs one benchmark on the board
through amix-box: `devmap` (q3dm1; escape1 without the briefing), a fixed
view (`setviewpos` for q3dm1), the world frozen (`timescale 0.001`), then
the average of com_speeds over the last 150 frames, on core 1's own clock
(from the PLL). Same scene every frame: the same build gives the same
number to 0.1 ms. A fresh home each run (only the key), c1q3's defaults.
Results go to `build/bench.txt`. In the image: `c1why` (why softpipe's fast
path refused draws) and `c1prof [on]` (softpipe's sp_fast_prof: cycles per
part, spans, pixels per path, L1 data refills with `on`), printed to the
log; the performance monitor is on from the start (as the gpu core).

| step | Quake III q3dm1 | RtCW escape1 |
|---|---|---|
| baseline 2026-11-09 (fresh config, GL_LINEAR_MIPMAP_NEAREST) | 224.9 ms | 437.3 ms |
| cleared tiles packed once (amix-gate 78317e1) | 224.9 ms (Q3 does not clear) | **412.2 ms** (-5.7 %) |

Profile of the frozen views (c1prof, 48 s each, 2026-11-09): spans ~75 % of
the time in both games, ~150 (Q3) / ~135 (RtCW) cycles a pixel, **1.2-1.3 L1
data refills a pixel**, 90-96 % of the pixels perspective-correct, ~1 M
pixels a frame (3.2x overdraw at 640x480). The pixels wait on memory, mostly
texels (two bilinear 2x2 footprints from row-major textures).

c1q3 sets `r_texturemode GL_LINEAR_MIPMAP_NEAREST` (as the zzgl launchers
did): with the engine's default trilinear, two-unit draws miss the fixed
spans (`c1why` reason 13: 6662 draws on a q3dm1 run in qemu).

## Next

1. Renderer: the span routines (amix-gate sp_fast, shared with the gpu
   core): NEON bilinear for both units, 4 pixels at a time; a timedemo to
   measure.
2. Done: q3.sh starts c1q3; Quake III (cde-apps/amixq3) in the AMIX panel.
3. Done: RtCW (games/iortcw) on the same image design, paks by request.
4. The generic pack path in RtCW's profile; a timedemo for both games.
