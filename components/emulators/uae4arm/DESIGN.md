# uae4arm on the Z3660's ARM core 1

An Amiga (A500, A600, A1200) emulated on the Z3660's second Cortex-A9 core,
bare metal, as its own core 1 program (`z3660ARM_Core_uae.bin`), beside AMIX on
the real 68060. The picture goes into the RTG overlay, the sound straight to
HDMI; AMIX hands over the Kickstart and the disks and passes keyboard, mouse
and joystick on. Nothing of the emulation runs on the 060.

```
 AMIX (68060)                                   Z3660 core 1 (Cortex-A9, bare metal)
 ----------------------------------             ---------------------------------------
 amixuae (Motif, the user)                      z3660ARM_Core_uae.bin
   | text lines on a pipe                         core1_main.c   control page, MMU, faults,
   v                                                             reload, pacing, overlay, HDMI
 c1uae (setuid root, drops root early)            c1uae.c        a minimal libretro frontend
   | /dev/zz9000: board memory + registers        libretro-core.cpp (upstream) -> uae4arm
   |   C1UA page 0x07AFD000  (commands, input ring, log)      CPU, chipset, floppy, hard file
   |   window   0x07100000  (files in, changed blocks out)  c1_syscalls.c  files in memory
   |   overlay  0x06000000/0x06800000 <------ picture ----   c1_thread.cpp  uae threads on libco
   |   HDMI     0x07CE0000  <------------------ sound -----
   v
 armbench -load/-reload (the core 1 loader, amix-gate firmware/core1)
```

## Choices

**Base: uae4arm-rpi (Chips), not Amiberry 8.5 and not FS-UAE.** Both are in
`distfiles/amiberry/` with their sha256. Amiberry 8.5's libretro build pulls in
PCem, a PPC core, QEMU's VGA, slirp, CHD, Lua and ImGui, and wants C++17 threads
and a file system. uae4arm-rpi is the same UAE lineage with the same ARM32 JIT
(`src/jit/codegen_arm.cpp`), about 120 files, written for the Raspberry Pi 1/2 (an
ARM11 or a Cortex-A7 at 700-900 MHz, much like one A9 at 666-1100 MHz). Its
libretro core is already single-threaded on libco: core 1 hosts it as a libretro
frontend. FS-UAE (SDL, OpenGL, threads) does not fit at all.

**The libretro API is the seam.** Upstream's `libretro-core.cpp` builds the
emulator's configuration from the model and the content. A three-line hook in our
patch takes any further uae config lines from the frontend, so the front end
names the Kickstart, DF0-DF3, hard files and CPU speed without new options in the
core. Video arrives as RGB565 through `video_cb`, sound as 16-bit stereo through
`audio_batch_cb`, input through `input_state_cb`.

**No threads, no file system, no signals on core 1:**

- *Threads* (`c1_thread.cpp`): uae's helper threads (the renderer, the hard file
  device) are libco coroutines. A semaphore with nothing to give lets the others
  run. A post from the emulator to a semaphore a worker waits on runs that worker
  at once, so a hard file request is done before the 68k goes on (no 20 ms per
  block).
- *Files* (`c1_syscalls.c`): newlib's `_open`/`_read`/`_lseek`/`_write`/`_stat`
  over files held whole in core 1's memory (about 240 MB of heap: even a 100 MB
  HDF fits). They come in through the 6 MB window at start. Writes mark 64 KB
  blocks, and AMIX fetches those back at its slow poll. The 060 must not poll
  board memory often (each of its reads stalls core 0's main loop), so there is
  no per-block request ring.
- *Signals*: none. The JIT needs a fault handler (below).
- *Memory*: no `mmap` at fixed addresses. The emulator takes its 16 MB 24-bit
  space from the heap (no Zorro III RAM yet; patch to `pandora_mem.cpp`).
- `compat/` has the headers newlib lacks (dirent, semaphore, sys/mman, dlfcn,
  glob ...), all doing nothing or failing cleanly. `c1_stubs.cpp` has the bodies,
  plus empty CD32 FMV, CD image and bsdsocket parts (no CD drives, no Amiga
  network on core 1).
- `int32_t` is `int` (`-D__INT32_TYPE__=int`), as in the GL cores: newlib says
  `long`, and uae's two type headers then disagree.
- zlib for ARM is built from `library/zlib`'s own pristine archive.

**Picture.** Core 1 converts the RGB565 frame to 0x00RRGGBB (0 becomes 0x010101,
since 0 is transparent in the overlay) into the back buffer and asks core 0 to
flip at vblank (`C1LD_W_FLIP`), as the GL server does. It never goes through X
(the 060 manages about 17 MB/s). The frame is 640x256 with line doubling (PAL),
so 640x512 on screen, or 1280x1024 with pixel doubling (the overlay has no
scaler). The overlay sits over the front end's window. Over the overlay the X
pointer cannot be seen; the Amiga draws its own (a sprite in the picture), and
the X pointer is grabbed and hidden while the Amiga has the mouse.

**Sound: HDMI straight from core 1** (checked in the firmware, `ax.c`): the audio
formatter's DMA plays a 30720-byte ring at board offset 0x07CE0000, 48 kHz
16-bit stereo little-endian, with its position in 0x7FC00128. vplay, the GL
server and the intros already write into it from core 1. The emulator renders at
48 kHz (`sound_frequency=48000`, so no resampling) and core 1 writes the samples
three periods (60 ms) ahead of the DMA. The DMA paces the frames, so the clock
is the sound, as in vplay; without a running DMA the timer paces at 50 Hz. c1uae
starts the DMA as vplay does (`REG_ZZ_AUDIO_PARAM` 0, `REG_ZZ_AUDIO_VAL`
0x07CE0000). No samples travel to AMIX. Paula on the A3000 (`/dev/paula`) stays
free for AMIX.

**Disks:** AMIX reads the ADF/HDF files (c1uae, as the user) and hands them
over whole. Compressed images (.adf.gz, .zip) are left to the core's own zlib
for now.

**Input:** the front end's window collects X key and mouse events and writes
them as 32-bit events into a 256-entry ring in the C1UA page (68k writes, core 1
reads before each frame; the 68k reads the tail only when the ring might be
full). Keys are libretro's codes, mapped from X keysyms in amixuae; the core maps
them to Amiga keys. Help is RETROK_INSERT, Left/Right Amiga RETROK_LMETA/RMETA.
The Amiga keyboard has neither Right Ctrl nor F11/F12, so the front end's own
key is **Right Amiga + Right Alt + Help** (gives keyboard and mouse back to CDE).
Joystick:

- *keyboard joystick* (in amixuae): cursor keys, Right Alt fire, Right Amiga second
  button, on port 2 (libretro port 0);
- *the A3000's own joystick port 2*: still to do. AMIX has no joystick driver; the
  stick's state is JOY1DAT (0xDFF00C) and the fire button CIA-A PRA bit 7 (POTGO
  for button 2). Either a small kernel driver (`/dev/joy`), or c1uae reads them at
  50 Hz if AMIX lets a root process map the custom chips. To check on the box; the
  protocol already carries `C1UA_EV_JOY` for both ports.

**Kickstarts:** only the user's own ROMs (`~/Documents/FS-UAE/Kickstarts`:
Kickstart 3.2 47.115 for A1200 and A3000, and `CDTVA500A600A2000.47.115.bin` for
A500/A600) or the free AROS ROM. A ROM in EPROM byte order (.bin: 0x14 0x11
0xF9 0x4E) is found by c1uae and swapped back on core 1. No ROMs or disks in git.

## The board protocol

`files/core1/c1uae_board.h` is the contract: the C1UA page (words big-endian),
the window, the overlay, the HDMI ring, the commands (CHUNK, START, STOP, FLUSH,
RESET, PAUSE, BENCH), the input events, the log ring. The image runs alone on
core 1 (not merged into the amix core), so it takes the player's page (ZZAV_OFF)
and ring. After STOP the image only waits for a reload: c1uae reloads it before
each run (`armbench -reload`, about a second; `-load` once after a power cycle).

## Building and testing

```
make prep             # pristine uae4arm-rpi + patches/*.patch
make build            # build/core1/z3660ARM_Core_uae.bin, build/host/uaehost.elf,
                      # build/amix/c1uae, build/bench/{c1bench.adf,coremark.060,coremark.000}
make check KICK=~/Documents/FS-UAE/Kickstarts/A1200.47.115.rom   # qemu-arm: CoreMark validates
make stage            # what goes on AMIX; files/amix/install.sh puts it there
```

`uaehost.elf` is the same ARM code under `qemu-arm -cpu cortex-a9`, with files
through semihosting, PPM frames, a WAV file and a virtual clock (20 ms a frame).
It checks function, not speed:

```
qemu-arm -cpu cortex-a9 build/host/uaehost.elf @ARGS    # one argument a line:
  -var uae4arm_model=A1200 -cfg kickstart_rom_file=ROM -frames 1500 -ppm 50 DIR -wav F SYSDIR DISK.adf
```

## Phase 0: what is measured, and how on the box

On the PC (qemu-arm, virtual time):

- Kickstart 3.2 (47.111/47.115) on the A1200 model boots `Giana_SE_MainGame.adf`
  (the user's copy, Scoopex's crack of the Timewarp release): the 68k runs in chip
  RAM from frame ~700, and the intro shows "The Great Giana Sisters SE from
  Timewarp Productions".
- `c1bench.adf` (CoreMark 1.01, 68000 code, 2K performance run, 300 iterations)
  boots, runs and validates ("Correct operation validated", crcfinal 0x5275).
  Cycle-exact A1200 (68020 at 14 MHz): 300 iterations in 2360 frames = 47.2 s of
  the Amiga's own time, i.e. **6.4 CoreMark for an emulated A1200**. That is
  emulation accuracy, not core 1's speed.

On the board, 2026-11-08 (core 1 at 866 MHz, interpreter, every step through
`amix-box`):

| run | result |
|---|---|
| `coremark.060 0x0 0x0 0x66 3000` (real 060, X running) | **55.1 CoreMark** |
| A500, `-bench 12000` (cycle-exact, unpaced) | 6.34 ms a frame = **316 % of real time** |
| A500 CoreMark (Amiga time, S to E marks) | ~144 s for 300 iterations = **~2.1 CoreMark** |
| A1200, `-bench 5000` | 3.04 ms a frame = **657 %**; CoreMark 47.2 s Amiga time = **6.4**, validated |
| A1200 `-cfg cpu_speed=max` | 300 iterations in 12.08 s on core 1 = **24.8 CoreMark**, validated |
| Giana SE, A500, run mode, overlay + HDMI | paced at PAL 50 Hz (~19 ms a frame); 45 underruns while loading, none after |

So the interpreted 68020 flat out is ~3.9x an A1200 and 0.45x the real 060;
an A500 or A1200 runs at full speed with room to spare. Faster than that needs
the JIT.

Known: with the A500 model, c1bench.adf reboots the Amiga after CoreMark's end
mark (in qemu-arm too, so it is the 68000 benchmark disk, not core 1); its
CoreMark figure comes from the marks. coremark.000 validates on the 060 but its
own timer is broken (ticks 0xFFFFFFFF): time it from outside.

```
c1uae -model A500  -kick .../CDTVA500A600A2000.47.115.bin -df0 /opt/uae/bench/c1bench.adf -bench 12000
c1uae -model A1200 -kick .../A1200.47.115.rom             -df0 /opt/uae/bench/c1bench.adf -bench 5000
c1uae -model A1200 -kick ... -df0 c1bench.adf -cfg cpu_speed=max -bench 5000   # the CPU flat out
/opt/uae/bench/coremark.060 0x0 0x0 0x66 3000                                  # the real 060
```

`-bench` reports the time per emulated frame (20 ms = full PAL speed) and the
share of real time, both against core 1's own clock (from the PLL, so right at
any ARM speed). With c1bench.adf it also gives CoreMark's run time on core 1 and
in the Amiga's own time. Every 5 s it prints the frames and core 1's timer
sample; after 20 s without a frame it says core 1 hangs and exits 4.

### The real image on the PC: files/qemu/run.sh

`qemu-arm` (user mode, `host_main.c`) tests uae and the frontend, NOT the core 1
program: no BSP start, MMU, interrupts, heap layout, memory files or board
protocol, and Linux system calls work there. The first board run hung in
exactly that gap (below). `files/qemu/run.sh` runs the real `build/core1/uae.elf`
in `qemu-system-arm -M xilinx-zynq-a9` (Cortex-A9, MMU, GIC, timers), its 1 GB
DDR a shared file in /dev/shm, and `fake68k.py` does c1uae's part on it (files,
chunks, config text, BENCH) at the board's offsets:

```
files/qemu/run.sh [-gdb] -model A500 -kick ROM -df0 build/bench/c1bench.adf -bench 300 -secs 90
arm-none-eabi-gdb build/core1/uae.elf -ex 'target remote :12345'   # with -gdb: stops it where it is
```

qemu-system-arm is Debian's 10.0 package unpacked into ~/opt/qemu (no root).
qemu's GIC is not enabled there (core 0 does that on the board), so the timer
samples stay 0 in qemu: use gdb.

The hang it found: the JIT's `flush_cpu_icache` is Linux `sys_cacheflush`
(`svc 0`, r7 0xf0002), and `build_comp()` calls it at start-up even with the
interpreter. On bare metal the svc lands in the BSP's empty SVC handler, which
spins in SVC mode with interrupts off: no fault, no sample, heartbeat frozen,
power cycle. patches/0002 does per-line cache maintenance instead (core 1
build only; the PC build keeps the syscall). No `svc` is left in the image.

## Next

1. **amixuae on the desktop** with Giana: keys, mouse grab, keyboard joystick, sound.
2. **The JIT on core 1.** uae4arm's JIT reaches the Amiga's memory directly
   (natmem) and relies on SIGSEGV for the I/O areas (`sigsegv_handler.cpp`
   decodes the ARM load/store, does it through the memory banks and patches the
   block). On core 1: put natmem in its own 1 MB-aligned sections, mark the
   sections of CIA (0xA00000-0xBFFFFF) and custom chips (0xD00000-0xDFFFFF) as
   faulting in the MMU table, and do the handler's work in the data abort
   exception (`fault()` in core1_main.c), returning to the patched code.
   Cortex-A9 has no SDIV/UDIV: never build with ARM_HAS_DIV.
3. Real joystick port 2 (driver or custom-chip read, see above).
4. Hard files: a writable HDF test (dirty blocks back through FLUSH), RDB HDFs.
5. NEON for RGB565 to 0x00RRGGBB, and pixel doubling in the overlay copy.
6. Fullscreen in amixuae; A600/A1200 IDE hard files (`cs_ide`).
