# One CPU target for every AMIX package: 68020..68060

Status 2026-11-16: analysis and proposal on the build host. No build default changed,
nothing committed, nothing run on the A3000. The A3000 measurement (section 7)
is ready and waits for the end of the week.

## 1. Verdict

Yes, one build can run on the 68020/030 (A2500/030, FS-UAE A3000/030) and the
68040/060 (the A3000 with the Z3660) without a measurable loss on the 060.
**The flag is not plain `-m68020-60`, though.** The right one is:

    -mcpu=68020 -mtune=68060 -fexcess-precision=fast

| build | 030-illegal insns | integer code vs -m68060 | FP code vs -m68060 |
|---|---|---|---|
| `-m68060` (today) | many (libX11: 1497) | - | - |
| `-m68020-60` | none in our code | identical except `lea` vs `adda.w` for the stack | **every float→int is 7 insns with 2 FPCR writes**; with `-ansi`/`-std=c99/c11` also doubles round-tripped through memory (libX11 Xcms +20 %, FFmpeg mpegaudiodsp_float +50 % insns) |
| `-mcpu=68020 -mtune=68060 -fexcess-precision=fast` | **none** | **byte-identical** (h264, IDCT, swscale, Xlib, Xt) | same instruction count; `fsadd`→`fadd`, `fdmul`→`fmul`, `fsmul`→`fsglmul`: same 060 latency |

Two conditions come with it (section 5):
1. the toolchain's own target libraries (libgcc.a, libstdc++.a, libamixc99.a)
   must be rebuilt for the baseline: today they hold 030-illegal code, so even
   a `-m68020-60` program (sysutils/amix-pkg) crashes a 030 in `vsnprintf`;
2. five hand-written 040/060 paths (fdsqrt, move16) need a run-time choice or a
   plain form. The new flag makes the assembler reject them, so none slips
   through.

Prediction for the 060 (section 6): 0 % for integer code, within ±1 % for FP
code, one micro-case (double→float store) is fewer instructions, not more.
The measurement in section 7 decides; the acceptance threshold is 2 %.

## 2. What the options do in gcc 14.2 (m68k)

From the m68k back end (m68k.h, m68k.md, m68k.cc of the GCC tree in
`usr-amix/upstream/m68k-amigaos-gcc/projects/gcc/gcc/config/m68k`, the same
conditions as stock 14.2; the plugin headers of our compiler match) and from
compiling `$SCRATCH/probe/t.c` all ways:

* `-m68020-60` = ISA 68020 (`TARGET_68040` false), tuning `u68020_60`, which
  makes `TUNE_68020`, `TUNE_68030`, `TUNE_68040` **and** `TUNE_68060` all true.
* `-mcpu=68020 -mtune=68060` = ISA 68020, tuning `u68060` only.

| what | condition in m68k.md | -m68060 | -m68020-60 | -mcpu=68020 -mtune=68060 |
|---|---|---|---|---|
| `fsadd/fdmul/fsmove...` (040/060 only) | `TARGET_68040` (ISA) | used | no | no |
| 32x32→64 `mulu.l`/`muls.l`, 64/32 `divu.l` (060 traps) | `!TUNE_68060` | avoided | avoided | avoided |
| inline `fsin`/`fcos` with -ffast-math (060 traps) | our patch `!TUNE_68040_60` | avoided | avoided | avoided |
| `fscale` for ×2^n (060 traps) | `!TUNE_68040_60` | avoided | avoided | avoided |
| float→int | `fix_trunc*` `TUNE_68040`, `ftrunc` `!TUNE_68040` | `fintrz`+`fmove.l` | **FPCR save, set RZ, `fmove.l`, FPCR restore (7 insns)** | `fintrz`+`fmove.l` |
| single-precision `*` `/` | `TARGET_68040` | `fsmul` `fsdiv` | `fsglmul` `fsgldiv` | `fsglmul` `fsgldiv` |
| `__FLT_EVAL_METHOD__` | `m68k_excess_precision`: `TARGET_68040` | 0 | 2 | 2 |
| macros | tuning | `__mc68060__` | `__mc68020__ ... __mc68060__` (all) | `__mc68060__` only |
| `as` accepts `fdsqrt`/`move16` | | yes | **yes** (silent) | **no: build error** |

Notes:
* `fintrz` is a 68881/68882 instruction and 060 hardware; only the 68040 traps
  on it (FPSP). Neither of our machines is a 68040; on one it would be correct
  but slow, exactly as today's `-m68060` code (which already uses `fintrz`).
* `fsglmul`/`fsgldiv` are hardware on the 68881/2, 040 and 060 (tools/amix-fpu-scan
  lists them as not trapping), same latency as `fsmul` on the 060.
* FLT_EVAL_METHOD 2 means intermediates stay in extended precision, as on a
  68882. In GNU C modes (`-std=gnu*`, gcc's default) that costs nothing. In ISO
  modes (`-ansi`, `-std=c99`, `-std=c11`, C++ `-std=c++17`) gcc's default
  `-fexcess-precision=standard` rounds every assignment through memory: X11R6.3
  builds with `-ansi`, FFmpeg with `-std=c11`. Hence `-fexcess-precision=fast`
  (identical code to the gnu modes, checked).
* Results of float code can differ in the last bit from the -m68060 build
  (extended intermediates, `fsglmul`). Nothing bit-exact depends on it that I
  found; libm99's own test (`make native-test`/`libm99-test`) must be rerun.
* `__mc68020__` is not defined with `-mtune=68060`; `__mc68060__` is. longlong.h
  (libgcc's 64-bit division) then takes the 060-safe C path, exactly as with
  -m68060 today. Code that wants to know the **ISA** must not use these macros
  (section 5).

### Why -mtune=68060 matters (Quake software renderer, -ffast-math)

insns / FPU insns / FPCR writes / fintrz per object, `$SCRATCH/quake/`:

| file | -m68060 | -m68020-60 | -mcpu=68020 -mtune=68060 |
|---|---|---|---|
| d_scan.c (span loop) | 775/225/0/22 | 903/271/**44**/0 | 775/225/0/22 |
| d_polyse.c | 1481/79/0/8 | 1522/96/16/0 | 1481/79/0/8 |
| d_sprite.c | 788/263/0/19 | 901/307/38/0 | 793/266/0/19 |
| r_aclip.c | 876/143/0/17 | 961/177/34/0 | 876/143/0/17 |
| d_part.c | 247/74/0/14 | 329/114/28/0 | 247/74/0/14 |
| sv_phys.c | 2645/636/0/14 | 2730/668/28/0 | 2639/625/0/14 |

Plain `-m68020-60` puts FPCR writes into the perspective span loops; an FPCR
write is a serialising FPU control move on the 060. Predicted: tens of percent
slower in such loops. `cpubench` (section 7) measures this directly (`plain`).

## 3. Instruction scans

Scanner: `$SCRATCH/tools/cpu-scan` (proposed as `tools/amix-cpu-scan`, an
extension of tools/amix-fpu-scan). Classes:
`030BAD` (not on 68020/030+68881/2: `fs*`/`fd*` rounding forms, `move16`),
`060TRAP` (as amix-fpu-scan), `040TRAP` (`fint`, `fintrz`). The `movep` hits
are gcc switch tables in .text (amix-fpu-scan explains it); they are identical
in every variant.

### x11r6.3 shared libraries (files/build-shared.sh, same tree, 4 ways)

| library | -m68060 | -m68020-60 (-ansi, standard) | -m68020-60 -fexcess-precision=fast | -mcpu=68020 -mtune=68060 -fexcess-precision=fast |
|---|---|---|---|---|
| libX11.so.7 | 030BAD 1497, 060TRAP 14 (movep), 040TRAP 3 | 030BAD 33 (libgcc), 060TRAP 14 | 060TRAP 14 | 060TRAP 14, 040TRAP 3 |
| libXaw.so.7 | 030BAD 109, 040TRAP 30 | 030BAD 33 (libgcc), 040TRAP 2 | 060TRAP 1 | 060TRAP 1, 040TRAP 30 |
| libXmu.so.7 | 030BAD 14 | - | - | - |
| libXt.so.7 | 030BAD 4 | - | - | - |
| ICE SM Xau Xdmcp Xext Xi, xauth | clean (switch-table movep only) | same | same | same |

Code size and difference against -m68060 (per function, `$SCRATCH/fn-*.txt`):

| library | -m68020-60 | -m68020-60 + fast | **-mcpu=68020 -mtune=68060 + fast** |
|---|---|---|---|
| libX11 | +4773 insns, 52 functions differ | +17, 2 differ, 6 FPCR writes | **0 insns, 0 functions differ** |
| libXt | identical | identical | **identical** |
| libXaw | +2900 | +148, 60 FPCR writes | **+1, 1 function** |
| libXmu | +28 | identical | **identical** |

The 30 `fintrz` in libXaw (scrollbar/pane geometry) are what -m68060 has too.

### FFmpeg 4.4.8 hot objects (build-ff's own compile lines, `$SCRATCH/ffmpeg/`)

Integer paths: h264_cabac, h264dsp, h264idct, h264_mb, h264pred, h264qpel,
hpeldsp, jrevdct, simple_idct, mpegvideo_motion, swscale, yuv2rgb:
**byte-identical disassembly** with `-mcpu=68020 -mtune=68060` (with
`-m68020-60` the only change is `lea d(sp),sp` → `adda.w #d,sp`, same cost).

Float paths (insns, -m68060 → -m68020-60 → proposed):
fft_float 4422 → 5592 → 4421; mpegaudiodsp_float 2297 → 3440 → 2297;
aacps_float 4813 → 5314 → 4810; resample 3264 → 4399 (6 FPCR writes) → 3262.
The proposed build changes only mnemonics, one for one (fft_float: 498
`fsmove.s`→`fmove.s`, 468 `fssub`→`fsub`, 354 `fsadd`→`fadd`, 274
`fsmul`→`fsglmul` ...).

### Toolchain target libraries (built with the configured default -m68060)

| file | 030BAD | note |
|---|---|---|
| libgcc.a | 542 | `__fixunsdfsi`, `__floatundidf`, `__extendsfdf2`, `__muldc3`... every program/library that converts unsigned/64-bit↔double links them |
| libamixc99.a | 1 | `amix_vsnprintf` (`fdmove.d`): any `%f` through snprintf |
| libstdc++.a | 124 | C++ (Blender, Motif-free C++ apps) |
| libsupc++.a, crtbegin/crtend, amix_init.o | 0 | |

Proof that this bites: `sysutils/amix-pkg` is built with `-m68020-60` today
and still holds 10 030-illegal instructions, from libamixc99's vsnprintf.

### Everything staged on the build host today (766 ELF files, `$SCRATCH/scan-installed.txt`)

Files with 030-illegal instructions (-m68060 builds), per component:
python 38/50 (116313 insns), runtime/python3 1 (2202), ffmpeg 3/3 (35055),
mesa-183 2/2 (29813), mesa-demos 494/494, glu 7124, quake 4335, libm99 4241,
sdl2 4179, fsv 2513, cde 54/74 (2295), x11r6.3 4/11 (1624), netsurf 1326,
libsvgtiny 1039, grafx2 813, motif 3/3 (756), quickjs-ng 696, openssh 12/12,
freeglut 505, gawk 451, vim 387, bison 351, libjpeg 229, wget 210, cde-apps 8/13,
the GNU tools 14/14, ncurses 9/11 ... Clean already (no FP in hot use):
freetype, libcss, libhubbub, libiconv, libnsbmp, libnsgif, libnspsl,
libnsutils, libparserutils, libutf8proc, libwapcaplet, sdl2-image, zlib,
libxp, libxrandr, xrandr. So: practically every package needs the rebuild;
there is no "only X" fix.

## 4. Inventory: where the CPU is set today

Compiler default: `components/developer/gcc-14/toolchain/build-cross.sh`
`--with-cpu=68060`, which the driver injects as `-mcpu=68060` whenever no CPU
option is given. So components with no flag (amixc99, dlmalloc, ncurses,
python3's modules via python-ext.mk, mesa-183, cde-apps parts ...) are 060 code
too.

| where | current flag | how |
|---|---|---|
| gcc-14 build-cross.sh | `--with-cpu=68060` | configure default (libgcc, libstdc++ built with it) |
| gcc-14 config.sh.example, amix-cc.sh, build-compat.sh (libamixc99), install-specs.sh | `AMIX_CPU="-mcpu=68060"` | config.sh |
| gcc-14 patch-gcc-060-fpu.py | `!TUNE_68040_60` | stays (true for both tunings) |
| make-rules/configure.mk | `AMIX_CPPFLAGS ?= -m68060 ...` | variable |
| make-rules/amix-toolchain.cmake | `CMAKE_C/CXX_FLAGS_INIT -m68060` | literal |
| make-rules/meson.mk | `c_args/cpp_args -m68060` | literal |
| make-rules/netsurf.mk | `NS_ENV CFLAGS -m68060` | literal |
| tools/amix-mkshlib | `-m68060` (so-init) | literal |
| x11/x11r6.3 Makefile, files/build-shared.sh | `AMIX_CPU ?= -m68060` | variable (build060.sh: literal, the old static set) |
| x11/libxp | `AMIX_CPU ?= -m68060` | variable |
| x11/libxrandr, libxrender, xrandr, mesa-demos/build.sh | `-m68060` | literal |
| desktop/motif, desktop/cde (Makefile + scripts/common.sh) | `AMIX_CPU ?= -m68060` | variable |
| desktop/cde-apps build.sh, amixcast/build-amixcast.sh | `-m68060` | literal |
| gnu/gnu.mk (all GNU tools) | `CC_BASE/CC_AMIX -m68060` | literal |
| library: bzip2, freetype, freeglut, glu, libiconv, libjpeg, libpng, zlib, amixstat, quickjs-ng, sdl2 (build.sh), sdl2-image, pth (Makefile + tools/*), libupnp, openssl (Makefile + configure-amix.sh), libm99, lib060, tinygl (config.mk, bench) | `-m68060` | literal |
| library/python/numpy, pygame | `-m68060` | literal |
| runtime/python3 build-python-amix.sh, python3.11-config.sh, stage-python-install.py | `-O2 -m68060` | literal; **also in sysconfig/`python3.11-config --cflags`, so every C extension a user builds on the box inherits it** |
| network: curl, wget, openssh | `-m68060` | literal |
| multimedia/ffmpeg configure-amix.sh, build.sh, compat/build.sh | `-m68060` | literal |
| web/netsurf Makefile | `-m68060` | literal |
| graphics: blender (build.py, build-deps.sh, link.sh, dnacheck.py), fsv, grafx2 | `-m68060` | literal |
| audio: libmikmod Makefile `-m68060`; its own mikmod/libmikmod Makefiles `-m68020` (gcc 2.7 native) | mixed | literal |
| audio/pt2-clone files/amix/build.sh | `-m68060` | literal |
| developer: nedit Makefile.amix, pkgconf configure-amix.sh | `-m68060` | literal |
| developer/vscode package.json, samples console-hello + cde-hello | default `-m68060` (gcc 14), `-m68020` (gcc 2.7) | setting / `CFLAGS ?=` |
| sysutils/amixtop | `-m68060` | literal |
| sysutils/amix-pkg | **`-m68020-60`** | literal (the FPCR variant; see section 1) |
| demos: amixintro, halloween, lamerintro; games: quake (4 Makefiles), ioquake3, iortcw; emulators/uae4arm | `-m68060` | literal; Z3660/core-1-only programs |
| gcc-27 (gcc 2.7.2.3) | `CPUFLAGS ?= -m68020` | separate toolchain, not affected |
| Xzz9000 server | gcc 2.7 default `-m68020` | not affected; already 030-clean |

95 files hold `-m68060`/`-mcpu=68060` (list: `$SCRATCH/cpu-files.txt`).

## 5. Source-level 040/060 code (needs a decision per item)

With `-mcpu=68020` the assembler rejects these, so the rebuild finds them:

| where | what | proposal |
|---|---|---|
| library/libm99 patches/0002 | `fdsqrt`/`fssqrt` under `__mc68040__ \|\| __mc68060__` | guard on the ISA (`__mc68020__` undefined and `__mc68060__` defined is not reliable: use a project macro, see below) or `fsqrt.x` + store (68882 and 060 hardware; double rounding only in rare halfway cases) |
| multimedia/ffmpeg compat/amix-math.c | `fdsqrt`/`fssqrt` under `__mc68000__` | same |
| x11/mesa-31 patches/0001 | move16 span fill under `__mc68060__ \|\| __mc68040__` | run-time choice as Xzz9000's rtgcpu.c (uname tag + `.word` encoding) |
| library/lib060 m060.c | `fdsqrt` | lib060 is 060-only by design (static, linked into core-1 games); keep, mark |
| library/soft3d s3d.c `to_board` | move16 as `.word` (scanner finds it) | only reached with a Z3660 (always a 060): keep, document as hardware-bound |

Proposed macro: the specs define `-D__amix_cpu_baseline__` (or the build passes
`-DAMIX_CPU_BASELINE`) so a source can tell "may run on a 030" from "060-only
build" without relying on gcc's tuning macros.

## 6. Predictions for the 060 (before any measurement)

| code | prediction (proposed flags vs -m68060) | why |
|---|---|---|
| integer code (Xlib/Xt requests, h264, IDCT, swscale, zlib, ssh, Python's interpreter loop) | 0 % | byte-identical instructions |
| float arithmetic (fft, AAC/MP3 synthesis, Mesa vertex math, Xcms) | 0 % ±1 % | one-to-one `fsadd`→`fadd`, `fsmul`→`fsglmul`, same 060 latencies, same count |
| float→int (Quake spans, Mesa F_TO_I, resample) | 0 % | `fintrz` kept by `-mtune=68060` |
| double→float stores | 0 % or faster | rounding happens in the `fmove.s` store, no `fsmove` before it |
| libgcc FP helpers after the rebuild | 0 % | same routines, plain FPU forms |
| `-m68020-60` (plain), for reference | f2i loop 2-4× slower; Quake d_scan ~20-40 % slower; FFmpeg mp3/aac float DSP up to +50 % insns (with -std=c11) | FPCR writes; excess-precision spills |

x11perf: the server (Xzz9000, gcc 2.7) is the same binary in both runs and
dominates every drawing test; client-side libX11 code is identical, so x11perf
should show 0 % within noise. xlibcpu (Xrm, quarks, XParseColor, text extents:
pure client-side Xlib) also 0 %. The only libX11 code that differs is Xcms
(colour management), which nothing hot uses.

## 7. Measurement plan for the A3000 (prepared, not run)

Kit: `$SCRATCH/boxtest/cpu-test.tar` (4.0 MB, sha256 in `cpu-test.tar.sha256`;
the unpacked tree is 3.9 MB):

    cpu/060/lib/lib*.so.7    X11R6.3 libs, -m68060 (rebuilt now, same tree)
    cpu/060/bin/x11perf xlibcpu cpubench
    cpu/base/lib/lib*.so.7   the same, -mcpu=68020 -mtune=68060 -fexcess-precision=fast
    cpu/base/bin/x11perf xlibcpu cpubench
    cpu/plain/bin/cpubench   -m68020-60, to measure the FPCR cost on the 060
    cpu/run-cpu-test.sh      the run (alternating order, players abort it, exit 75)
    compare.py (on the build host)  medians, base/060 ratio, FAIL < 0.98

cpubench (`$SCRATCH/bench-src/cpubench.c`) times the patterns that differ:
`f2i d2i fdot ddot d2f fdiv span` and the controls `int mem`. xlibcpu is the
repo's `components/x11/x11r6.3/files/xlibcpu.c`. x11perf is R6.3's, with
`-noop -prop -getimage100 -dot -rect10 -rect100 -fcircle10 -line10 -seg10
-ftext -copywinwin100 -putimage100 -shmput100` (`-fill10` does not exist in
R6.3; `-rect10` and `-fcircle10` stand in for it). Libraries come in through
`LD_LIBRARY_PATH=/var/tmp/cpu/<variant>/lib`; /usr/x11r6/lib is not touched.

Steps (main session, each through `amix-box`, only with the user's OK, nobody
playing; the run draws on the A3000's own X screen):

1. `amix-box -m "cpu test: df" ssh root@$AMIX_BOX df -k / /var/tmp` (`$AMIX_BOX`: the A3000's address)
   (>= 200 MB free on /, the kit needs 4 MB in /var/tmp).
2. Note the Z3660 clock as the box tools report it (timing-from-actual-clock):
   both variants run in the same session, so the ratio holds, but the note
   makes later numbers comparable.
3. `amix-box -m "cpu test: copy" scp cpu-test.tar root@...:/var/tmp/` then
   `sha256sum`/`cksum` check, `cd /var/tmp && tar xf cpu-test.tar && rm cpu-test.tar`.
4. With X running (dtlogin session, DISPLAY=:0 for root, or the user's
   xauth): `amix-box -w 60 -m "cpu test: run" ssh root@... 'DISPLAY=:0 /var/tmp/cpu/run-cpu-test.sh 3'`.
   3 rounds, order 060/base, base/060, 060/base; about 25 min (13 x11perf
   tests × 3 × 3 s + calibration per variant and round; cpubench with
   SCALE=20 is about 10-20 s per kernel set; set SCALE=10 if slower).
5. `scp` back `/var/tmp/cpu/results-*.txt`, then `rm -rf /var/tmp/cpu` at once.
6. On the build host: `python3 -I compare.py results-*.txt`.

Acceptance: no x11perf test, no xlibcpu line and no cpubench kernel with the
baseline more than 2 % slower than -m68060 (median of 3 rounds × 3 x11perf
repeats). If a kernel fails, rerun only it (5 rounds) before concluding.
`plain` is information only (expected: f2i/d2i/span clearly slower).

Optional later (bigger, after the toolchain rebuild): FFmpeg `-benchmark`
decode of the same clip, Quake software timedemo, Python pystone, openssl
speed, each built both ways.

FS-UAE A3000/030 functional check: not run in this pass. The kit's `base/`
directory is exactly what the existing harness
(`components/x11/xzz9000/log/fsuae-picasso2-rtgtest.sh`, rtgtest.tar layout
`rtgt/lib/*.so.7`, `rtgt/x11perf`) takes; replace its libs with `cpu/base/lib`
and its x11perf with `cpu/base/bin/x11perf` on a disposable disk copy.

## 8. Flexible CPU architecture (design)

### What the platform offers

* **ld.so.1 (AMIX SVR4.0, in libc.so.1):** honours `LD_LIBRARY_PATH` and the
  object's RPATH/RUNPATH (both in use: X test clients, halloween's runpath).
  No `$ORIGIN`, no hwcap directories, no platform subdirectories: none of the
  strings exists in libc.so.1 (Solaris added `$ORIGIN` in 7, hwcap later).
  dlopen works but has the NEEDED-order trap (amix-fix-needed).
* **Kernel CPU tag:** `uname(2)` machine string, port kernel
  "Amiga (Unlimited) 68040-<build>", '4'→'6' when cputype is 60
  (amix-gate port/src/inituname040.s). The stock 68030 kernel has no tag; a
  " 68030-" tag is planned for the one-kernel 030 path. No tag = baseline.
* **Precedent:** Xzz9000's `rtgcpu.c` (local-rtg-cardops.patch) parses " 680x0-"
  once, allows move16 only for 040/060, spells the instruction as `.word`,
  and has `-move16/-nomove16` overrides; the card modules are chosen at run
  time (MODULAR-X-SERVER-EVALUATION.md, option B). lib060 is a link-time
  choice (static archives), not a run-time one.
* **Packaging:** IPS variants/facets and AMIX pkgadd choices are made at
  install time. Our disks move between machines (the A3000's HDF copies boot in
  FS-UAE as a 030), so an install-time CPU choice is wrong as soon as the disk
  moves.

### Recommendation

1. **Baseline for every package:** `-mcpu=68020 -mtune=68060
   -fexcess-precision=fast`, one build, one package. On the evidence above
   this already is the 060-tuned build for compiled C/C++; a whole-library
   -m68060 variant would add the 030-illegal rounding forms and gain nothing.
2. **Tuned paths only inside a library, chosen at run time.** One shared
   helper `amix_cpu()` (in libamixc99, static, so no new shared dependency)
   that parses the uname tag once (rtgcpu.c's rule; 30 when no tag) with an
   environment override for tests (`AMIX_CPU=30|40|60`), plus the asm spelled
   as `.word` or under a local `.cpu 68060`. The library sets its function
   pointers in its so-init constructor (tools/so-init) or on first call.
   Cost: one indirect call per dispatched function, a few hundred bytes.
3. **Whole-library variants (only if a measurement ever shows > 2 %):**
   `<libdir>/060/lib*.so.N` next to the baseline, RUNPATH
   `<libdir>/cpu:<libdir>`, and `<libdir>/cpu` a symlink to `060` or to an
   empty directory, set by one rc2.d script from the uname tag at every boot.
   ld.so.1 then finds the tuned copy first and falls back to the baseline;
   a disk moved to a 030 corrects itself at the next boot. Both variants in
   the same package (no install-time choice). Cost: twice the library size,
   one boot script, and every program needs the RUNPATH. Not recommended now.

Candidates for (2), from the code comparison: Mesa's move16 span fill
(mesa-31 patch), sqrt in libm99/FFmpeg compat (fdsqrt vs fsqrt: a few cycles),
and lib060's trap-free sin/cos/atan/pow (on a 030, AMIX's libm with FSIN/FMOVECR
is native and fine; on the 060 those trap at ~78 us: worth a dispatch if lib060
is ever made a shared library). Not candidates: Xlib/Xt/Xaw/Motif/CDE,
FFmpeg (integer paths identical; its float DSP only renamed), Python, GNU
tools, OpenSSH/OpenSSL, zlib, NetSurf libraries.

## 9. The change I propose (not made)

One place sets the CPU:

1. `make-rules/shared.mk`:
   `AMIX_CPU ?= -mcpu=68020 -mtune=68060 -fexcess-precision=fast`
   and every recipe uses `$(AMIX_CPU)` (configure.mk's AMIX_CPPFLAGS,
   amix-toolchain.cmake via a variable, meson.mk's c_args/cpp_args,
   netsurf.mk, gnu.mk, python-ext.mk, tools/amix-mkshlib via the environment;
   the literal `-m68060` in the 60-odd Makefiles/scripts of section 4 replaced
   by `$(AMIX_CPU)`/`$AMIX_CPU`; python3's sysconfig and python3.11-config
   carry the same string).
2. Compiler default, so that nothing can fall back to 060 code:
   build-cross.sh `--with-cpu=m68020` (instead of 68060) and in
   install-specs.sh's self_spec
   `%{mcpu=68020:%{!mtune=*:-mtune=68060}} %{!fexcess-precision=*:-fexcess-precision=fast}`
   (tested with `-specs=` on today's compiler: `-m68020` and `-mcpu=68020`
   then give exactly the proposed code; `-m68060` stays honoured). Then
   rebuild libgcc, libstdc++/libsupc++ (rebuild-target-libs.sh) and
   libamixc99 (build-compat.sh), and scan them clean. The README's
   "--with-cpu=68060 is a correctness setting" argument (no 64-bit divide in
   libgcc) holds unchanged: `-mtune=68060` keeps `TUNE_68060`, and longlong.h
   still sees `__mc68060__`. Check after the rebuild that the injected default
   is seen by the self_spec (`gcc -### -c t.c` shows `-mtune=68060`).
3. A build gate: `tools/amix-cpu-scan` (the scanner above) run by shlib.mk /
   the stage rules: 030BAD > 0 fails the stage, except components that declare
   `CPU_SPECIFIC = 060` with a reason (lib060, soft3d/zzgl, core-1 games and
   demos, uae4arm's 060 parts).
4. Per-component overrides only with a reason in the Makefile: the Z3660/core-1
   programs (they cannot run without a 060), lib060, and benchmark builds.
5. Fix the five source items of section 5; drop `-m68020-60` from amix-pkg.
6. Rebuild order: toolchain libs → libamixc99 → x11r6.3 → everything else by
   dependency (the meta packages' member lists), each scanned. Re-run
   libm99/lib060 tests, FFmpeg/vplay playback and the CDE login on the box.

Switch only after section 7 passes on the A3000.

`$SCRATCH` = a scratch folder on the build host, not in this repository
(25 MB: kit, scanner, stages, objects; the X source copies were deleted).
