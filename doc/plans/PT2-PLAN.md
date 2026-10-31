# pt2-clone (ProTracker 2.3D clone) on AMIX: feasibility, 2026-10-24

Source: upstream/pt2-clone (git clone --depth 1, 18954f2, 2026-09-17), BSD-3.
29,400 lines of C, plus gfx/ (built-in bitmaps as C arrays) and the loaders.

## What it needs, measured
- **SDL2, about 120 distinct calls.** Window, renderer and one streaming
  ARGB8888 texture (320x255, scaled); keyboard (scancodes) and mouse events;
  cursor; SDL_OpenAudioDevice with AUDIO_S16 and a callback; performance
  counter, SDL_Delay; SDL_ShowSimpleMessageBox; hints; SDL_HasSSE/SSE2.
- **Threads: three, all SDL_CreateThread + Detach.**
  - scopes (pt2_scopes.c), the one that runs all the time;
  - the disk-op file lister (pt2_diskop.c);
  - MOD2WAV export (pt2_mod2wav.c).
  Plus the SDL audio callback, which runs on SDL's audio thread.
- **Endianness: contained.**
  - SWAP16/32/64 are unconditional (they assume a little-endian host): 23 uses
    in the MOD/sample savers, the IFF/AIFF loaders, xpk and config.
  - About 50 `fread(&var, ...)` and 30 pointer-cast reads, the WAV loader among
    them, assume little-endian.
  - Fix: host-order helpers (be16/le16/...).  The MOD format itself is big-endian.
- **Audio:** mixes at 44.1-192 kHz (MIN_AUDIO_FREQUENCY 44100) to S16 with BLEP
  synthesis and A500/A1200 RC filters, mostly in double.  On the 060 it would
  run at the Paula rate (about 27.7 kHz), outputting 8-bit signed to
  /dev/audio (AUDIO_STEREO).
- **CPU check:** main() refuses to run without SSE/SSE2; drop that on m68k.

## Plan
1. **libSDL2-amix**, a small SDL2 subset (gcc14-cross-amix, userland only):
   - X11 R6.3 window plus XPutImage blit, converting ARGB8888 to the visual
     (16 bpp on the Z3660 RTG);
   - X key and mouse events, translated to SDL scancodes;
   - audio: /dev/audio fed from the main loop, no audio thread;
   - SDL_GetPerformanceCounter from gettimeofday.
2. **Threads.**  AMIX has no pthreads.
   - scopes: run their function from the main loop each frame;
   - file lister, MOD2WAV: run synchronously (the UI blocks while they work).
3. **Endian-clean loaders and savers** (MOD first, then WAV/IFF/AIFF).
4. **Later: performance tuning.**  Possibly a lighter mixer, or playing
   through real Paula channels instead of mixing in software.

Prerequisite: the AUDIO_STEREO kernel (i7) works on the A3000.
