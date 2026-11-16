# One X11R6.3 server for every AMIX graphics card (evaluation, 2026-11-16)

Goal: a full virtual AMIX development environment (AMIX Installer + FS-UAE)
**and** real machines, with one X11R6.3 server that is not bound to one card.
The server keeps everything we added to R6.3: RandR 1.1 + the CDE display
panel, MIT-SHM, the blitter and move16 paths, pattern fills, the bell,
VT switching and the soft3D swap extension. CDE and MWM then install the same
way on every machine.

## 1. What exists today

| Piece | State | Bound to |
|---|---|---|
| `components/x11/xzz9000` (our R6.3 server, `Xzz9000`) | in use on the A3000 + Z3660 | ZZ9000 register interface (`/dev/zz9000`, major 49). The Z3660 RTG keeps those registers, so one driver serves both boards |
| DDX layout `hw/amix/` + `hw/amix/rtg/` + `hw/amix/rtg/zz9000/` | from jusii's amix-z3660rtg, which follows asokero's Xrtg layout | `rtgInit.c` calls `ZZ9000InitHW()` directly; `rtg.h` assumes RGB565 (`unsigned short *fbBase`) |
| `amixInit.c` `amixFbData[]` | a probe table: native chipset mono (`amixMono`), TIGA stub, RTG | already the right idea (probe in order, first card wins), but the RTG entry has one card |
| Our local improvements | `local-*.patch` in xzz9000 | **most of them patch `rtg/zz9000/zz9000draw.c`**: CopyWindow order, PaintWindow tile, pattern fills, PutImage move16, soft3D swap. Only RandR (DIX + `rtgRandR.c`) and the bell are outside the card |
| Kernel drivers (amix-gate `usr/src/uts/amiga/io/`) | zz9000 (+ Z3660 target), soft3d | no generic RTG device |

So the server is *structured* for several cards but *built* for one, and our
optimizations live inside that one card's code.

## 2. Cards to support

| Card | Chip | Where | Kernel driver | X driver | Licence |
|---|---|---|---|---|---|
| MNT ZZ9000 | FPGA, ZZ9000 regs | real A3000/A4000 | ours (zz9000, major 49) | ours (zz9000) | MIT |
| Z3660 RTG | same regs on the Z3660 | real A3000/A4000 | ours (`-DZZ9K_TARGET_Z3660`) | ours (zz9000) | MIT |
| **MNT VA2000** | FPGA, own regs, blitter, no HW cursor; 8 and 16 bit; Z2 (4 MB) and Z3 (32 MB) | real machines | **asokero/va2000-amix** (major 68, `/dev/va2000`, SVGAIOC numbering), MIT | asokero/xrtg-amix `rtg/va2000/` (X11R5; same rtg layout as ours) | driver MIT; **Xrtg has no licence file (NOASSERTION)**: needs asokero's OK before we import its code |
| **Cirrus Logic GD542x/543x/5446**: Picasso II/II+ (5426/5428), Picasso IV (5446), Piccolo (5426), Piccolo SD64 (5434), Spectrum 28/24 (5428) | VGA-class, BitBLT engine, HW cursor; 8/16/24 bit | real machines **and FS-UAE** | none on AMIX yet | none for R6.3; Klaus Burkert's Xsvga (X11R5, Gateway CD 2) did Piccolo/Picasso II | Xsvga: unknown licence, reference only; XFree86 3.3 `cirrus` driver (MIT/X) is the code source |
| FS-UAE `uaegfx` | UAE-only RTG (trap calls into the emulator) | FS-UAE only | would need UAE traps from AMIX | new | — |
| Native chipset (ECS/AGA) | planar | everywhere, incl. FS-UAE | stock AMIX screen manager | `amixMono` (1 bit) | stock |
| CyberVision 64 (S3 Trio64) | S3 | real only (not in FS-UAE 3.2.35) | none | XFree86 3.3 `s3` as source | later |

**FS-UAE 3.2.35** (checked in its `gfxboard.cpp`) emulates at register level:
Picasso II, Picasso II+, Picasso IV Z2/Z3, Piccolo Z2/Z3, Piccolo SD64 Z2/Z3,
Spectrum 28/24 Z2/Z3 (all through QEMU's `cirrus_vga`), the A2410 (TIGA) and
uaegfx. `graphics_card =` offers uaegfx, picasso-ii, picasso-ii+, picasso-iv;
the others are reachable with `uae_gfxcard_type = PiccoloSD64_Z3` and so on.
Picasso IV needs its ROM file (`picassoiv_rom_file`) for AutoConfig **(check
whether AMIX needs it at all)**.

**Consequence:** one **Cirrus driver** covers the virtual environment *and*
five real cards. It is the most valuable new driver. Picasso IV Z3 (GD5446,
4 MB, up to 1280x1024x16) is the target for FS-UAE; it needs a Zorro III
capable kernel (stock AMIX cannot reach Z3 boards, the 040/060 port kernel
can). Picasso II (Z2, 2 MB, 1024x768x8 or 800x600x16) also runs on stock
AMIX kernels.

## 3. Options for "not bound to one card"

### A. All cards compiled in, chosen at run time
Refactor first (needed by every option): a card **ops table** in `rtg.h`, a
card list in `rtgInit.c`, probe in order (or `-card NAME`, or the
`/etc/X11/amix-card` file the installer writes).
- + simplest, works with the static R6.3 server today, no loader ABI.
- − every new card needs a new server build; all cards in every package.

### B. Loadable card modules (`dlopen`)
Core server + `/usr/x11r6/lib/modules/rtg/<card>.so`. The core passes the
module a **versioned callback table** (`rtgCoreFuncs`: ErrorF, region and
mi/cfb helpers, `rtgRegisterMode`...) and gets back a `rtgCardRec`. The module
never needs symbols exported from the executable, which avoids SVR4 ld.so's
limits on symbols from the program (and our dlopen NEEDED-order trap,
see amix-link-stubs).
- + one server package, card packages separate (`x11/driver/rtg-cirrus`...),
  third-party cards without rebuilding the server: that is the "modular"
  the user asked for.
- − a small ABI to keep stable (version number + size check at load).
- Needs a test first: dlopen of a `.so` from the R6.3 server on AMIX, with
  our shared libc.

### C. One server binary per card over a shared core (XFree86 3.3 style)
`Xamix-zz9000`, `Xamix-cirrus`, ..., all linked against `libXamixcore.so`;
`/usr/x11r6/bin/X` points at the right one.
- + robust, no loader.
- − the "which X" choice moves to the installer and to every card change;
  RandR/extension code is duplicated across binaries unless it is all in the
  shared core.

**Decision (2026-11-16): B, built on the A refactor; X server only.** The
modules are user-space `.so` files loaded by the X server. They do **not**
touch the kernel: no loadable kernel modules (jusii's kernel module loader,
with its untested >1 GB claims, is not used). The kernel keeps our own design:
drivers built in, and a driver for absent hardware costs nothing (it finds no
board at AutoConfig and stays idle). Step 1 is the ops table with
the zz9000 driver compiled in (option A, nothing visible changes). Step 2
moves the zz9000 code into a module and proves the loader. Every later card is
then only a module + a kernel driver. If the dlopen test in step 2 fails on
AMIX, fall back to A without losing work.

## 4. Where each improvement lives after the split

| Today (in zz9000 code) | Moves to | Card supplies |
|---|---|---|
| CopyWindow box order fix | core (`rtgwin.c`) | `copyRect(src, dst, w, h)` blitter op, or nothing (CPU copy) |
| PaintWindow tiled backgrounds | core | `solidFill`, optional `patternFill` |
| FillTiled / Stippled spans | core | optional `patternFill`, `monoExpand` |
| PutImage 16-bit move16 | core (needs only the linear framebuffer + pitch) | nothing; `cpuCaps` says whether move16 is safe (also the move16 hang on the 528 MB kernel: one switch, not per driver) |
| RandR 1.1 + `rtgRandR.c` | core already | `getModes()`, `setMode(w, h, depth, hz)` |
| Bell (`/dev/audio`) | core (amix input) | nothing |
| VT switch / monitor switch | core | `monitorSwitch(rtg|native)` |
| soft3D swap extension | **card extension** in the zz9000 module | the module registers it if the board has soft3D |
| HW cursor | core uses mi software cursor unless | `cursorSet/Move/Show` (Cirrus yes, VA2000 no, ZZ9000 yes) |
| MIT-SHM | core | nothing |

Depth: the core must handle **8-bit PseudoColor** (VA2000, Cirrus 2 MB boards,
native chipset) and 16-bit TrueColor; 24/32 later as in
`DEPTH-24-32-EVALUATION.md` (Z3660 HDMI stays 565). Today `rtg.h` hardcodes
16 bit (`unsigned short *fbBase`): that is the main core change, using the
R6.3 `cfb` (8 bit) and `cfb16` paths as the CPU fallback.

## 5. Kernel side

Each card needs an AMIX kernel driver. Two ways:
1. **One driver per card** (zz9000 today, va2000 from asokero, a new cirrus).
2. **A small generic one**, `rtgfb`: AutoConfig lookup by manufacturer/product,
   maps registers + framebuffer for `mmap` with the right cache class, the
   monitor switch, and nothing else; mode setting and the blitter run in the
   user-space module (as XFree86 does on PCs).

Decision: keep zz9000 (it has firmware-specific ioctls); write **one small
built-in `rtgfb`** for the other boards (Cirrus first, VA2000 later),
with mode setting and the blitter in the user-space X module. `rtgfb` does
nothing on a machine without such a board, so it fits the
one-kernel-for-030-to-060 line and the "absent hardware costs nothing" rule.
asokero's va2000-amix (MIT) is the reference for the VA2000 registers and
cache class; whether its own driver is imported instead is decided when the
VA2000 comes up. Major numbers: zz9000 49,
random 48, paula 53, va2000 68 (asokero's): check 68 against our kernel's
table before import.

## 5a. Capabilities come from the kernel driver (user, 2026-11-16)

"It makes perfectly sense for our X drivers that those capabilities are being
propagated so we know if console redirects work before even trying them on a
card that does not have that feature."

Every RTG kernel driver (zz9000, rtgfb, later va2000) answers one common
ioctl from a shared header `rtgcaps.h` in amix-gate `io/`:

```c
#define RTGIOC_GETCAPS   (('R'<<8)|1)   /* same number in every RTG driver */
struct rtg_caps {
    unsigned short version, size;        /* struct grows at the end only */
    char           board[24];            /* "Z3660 RTG", "ZZ9000", "Picasso II" */
    unsigned long  caps;                 /* RTGCAP_* below */
    unsigned long  depths;               /* bit n = depth n available (8, 16, 24) */
    unsigned long  fb_size, max_width, max_height;
};
RTGCAP_CONSOLE_REDIRECT  /* kernel console can be drawn on the card */
RTGCAP_MONITOR_SWITCH    /* Amiga/RTG switch */
RTGCAP_BLIT_FILL, RTGCAP_BLIT_COPY, RTGCAP_BLIT_PATTERN  /* in the kernel or card */
RTGCAP_HW_CURSOR, RTGCAP_CLUT, RTGCAP_SOFT3D, RTGCAP_MODE_LIST, RTGCAP_REFRESH
```

- The X core asks it once in `rtgProbe` and sets the card's caps from it
  (intersected with what the X card code implements); RandR, the depth
  choice, the cursor and extensions use only what both sides have.
- Tools and rc scripts ask before acting: `S99zz9000` and `zz9k_test
  redirect` switch the console only if `RTGCAP_CONSOLE_REDIRECT` is set and
  say nothing otherwise; a console tool on a Picasso II says "this card has
  no console redirect" instead of failing an ioctl.
- A small `rtginfo` command prints the board and its capabilities (for
  people, the installer and bug reports).
- zz9000: the caps depend on the board (Z3660: 16 bit only, soft3D; ZZ9000:
  8/16/24, no soft3D); rtgfb: per board table (Picasso II: monitor switch,
  CLUT, depth 8, blitter in the card, no console redirect yet).
- Old kernels without the ioctl: the X core falls back to the card's static
  caps, as today.

- `x11/xserver-amix` (core server, RandR, SHM, input), and one package per
  card: `x11/driver/rtg-zz9000` (ZZ9000 + Z3660), `rtg-va2000`, `rtg-cirrus`,
  later `rtg-uaegfx`, `rtg-s3`. Each card package depends on its kernel
  driver package. A meta package pulls core + all drivers for a generic disk.
- AMIX Installer: "Graphics card" choice per target. In FS-UAE mode it reads
  the FS-UAE configuration's `graphics_card` and picks the module (or writes
  `graphics_card = picasso-iv` into the configuration it creates). The chosen
  card name goes to `/etc/X11/amix-card`; the server probes all modules if the
  file is missing.
- CDE and MWM then need nothing card-specific; the CDE display panel works on
  every card through RandR.

## 7. Order of work

1. Refactor: `rtgCardRec` ops table, 8/16-bit core, our zz9000 improvements
   moved to the core. zz9000 still compiled in. Test on the A3000 + Z3660:
   same speed (x11perf baseline), CDE, RandR panel.
2. dlopen test on AMIX; zz9000 as a module.
3. Cirrus module + kernel side, tested in **FS-UAE** first (Picasso IV Z3 and
   Picasso II), then on a real Picasso/Piccolo if one is at hand.
4. VA2000 module: ask asokero for a licence on Xrtg's va2000 code (or write it
   from the VA2000 register documentation and the MIT kernel driver).
   No VA2000 here to test on; asokero has one.
5. Packages + installer card choice; CDE/MWM on the FS-UAE image.
6. Later: uaegfx (fast in FS-UAE, emulator only), S3/CyberVision 64,
   native chipset at more than 1 bit.

## 8. Open points for the user

1. ~~Option B~~: decided, X-server modules only, kernel unchanged in design
   (user left the choice to me, 2026-11-16, ruling out kernel-side loadable
   modules).
2. Licence for Xrtg's VA2000 X code: the user asked asokero on Discord
   (2026-11-16); waiting for the answer. Until then nothing from Xrtg is
   copied.
3. ~~FS-UAE target card~~: **Picasso II** (user, 2026-11-16): a real
   Picasso II goes into the A2500/030 (A2630, BigRAM, A2091, BlueSCSI), so
   FS-UAE emulates the same card (`graphics_card = picasso-ii`, GD5426,
   2 MB, Zorro II). One Cirrus module is tested in FS-UAE and then runs
   unchanged on the A2500. Consequences:
   - the 8-bit PseudoColor core is required, not optional: 2 MB gives
     1024x768x8 and 800x600x16; 16 bit at 1024x768 needs more pixel clock than
     the GD5426 has **(check the 5426's 16-bit clock limit)**;
   - the A2500/030 has no move16 (68030): the core's move16 path must be off
     there (`cpuCaps`), with the plain copy as the default;
   - Zorro II only, so no Zorro III support is needed; it runs on our one
     kernel (030 to 060) on the A2500, and in FS-UAE;
   - a 68030 without the Z3660's speed: the Cirrus BitBLT engine for fills and
     copies matters even more than on the Z3660.
