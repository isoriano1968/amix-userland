# Port overlay

`overlay/xc` is copied over a pristine X11R6.3 `xc` source tree.

## Build configuration

- `config/cf/amix.cf`: AMIX platform, GCC 2.7.2.3, shared libraries, no MIT-SHM
- `config/cf/amix.rules`: old cpp and AMIX archive compatibility rules
- `config/cf/Imake.cf`, `config/cf/site.def`: AMIX platform selection
- `config/imake/Makefile.ini`, `config/imake/imakemdep.h`: bootstrap compiler/cpp
- `config/util/amix-shared-rebuild.sh`: clean PIC library and client rebuild
- `config/util/startXzz9000`, `xinitrc.zz9000`: build-tree twm test session without using crashing AMIX `xinit`; detaches console output before X and restores the ZZ9000 console path on exit
- `programs/Imakefile`, `programs/rstart/Imakefile`: old cpp/make compatibility
- `programs/twm/lex.l`: AMIX lex compatibility for the revision comment

## X server

- `programs/Xserver/Imakefile`: `Xzz9000` target and AMIX DDX dependency graph
- `programs/Xserver/include/servermd.h`: m68k AMIX image/bitmap padding and order
- `programs/Xserver/dix/dixfonts.c`: font pad probe helpers for catching stale font objects
- `programs/Xserver/mfb/maskbits.h`: disables the broken AMIX/m68k inline-asm bitfield fast path
- `programs/Xserver/mfb/mfbimage.c`: AMIX-safe 1bpp image wrapper for glyph/cursor bitmaps
- `programs/Xserver/mi/miglblt.c`: glyph bitmap diagnostics for GLYPHPADBYTES/padding verification
- `programs/Xserver/mi/midispcur.c`: software cursor path used by the ZZ9000 RTG fallback
- `programs/Xserver/hw/amix`: AMIX keyboard, mouse, console, and server glue
- `programs/Xserver/hw/amix/rtg`: generic AMIX RTG screen layer
- `programs/Xserver/hw/amix/rtg/zz9000`: ZZ9000 16-bit framebuffer DDX
- `lib/font/bitmap/pcfread.c`, `bitmapfunc.c`: PCF/font diagnostics for glyph padding bring-up

`SHARED-LIBRARIES.md` documents the native AMIX `.so`/`.sa` ABI and required
left-to-right client link order.

Generated Makefiles, object files, libraries, executables, fonts, and the
upstream X11R6.3 distribution are intentionally not tracked.
