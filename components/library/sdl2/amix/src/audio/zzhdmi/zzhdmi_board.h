/*
  zzhdmi_board.h -- sound on the Z3660's HDMI through core 1's GL server,
  for programs without a GL session of their own (SDL's "zzhdmi" audio
  driver, pt2-clone's sdl2-amix).  libmikmod's drv_hdmi.c, as a helper.
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.

  The 060 writes 16-bit stereo frames (native, big-endian) into a ring in
  board memory at the byte offset of their time; core 1 plays them, 120 ms
  ahead of the HDMI DMA, doubled to 48 kHz when the stream runs at 24.
  Times are stream bytes since zzhdmi_open() (frames * 4), 64-bit.
*/
#ifndef ZZHDMI_BOARD_H
#define ZZHDMI_BOARD_H

#define ZZHDMI_RING_BYTES   (32768UL * 4)   /* zzgl.h's ZZGL_SND_SIZE: 1.4 s at 24 kHz */
#define ZZHDMI_CORE1_LEAD_MS 120            /* core 1 reads this far ahead of what is heard */

/* 1: core 1 runs the GL server's program (gpu or amix) and no other
   program is writing the HDMI stream.  Takes up to 150 ms when a stream
   is switched on (to see whether its writer is alive). */
int zzhdmi_present(void);

/* div 1: 48000 Hz, 2: 24000 Hz; keep: the most bytes the caller writes
   ahead of what is heard.  Each write also zeroes the stream a lap ahead
   less keep: a stalled writer leaves silence, not the last lap's sound,
   for about 1.4 s (24 kHz) less keep.  0 on success. */
int zzhdmi_open(int div, unsigned long keep);

/* stream bytes the HDMI has played since zzhdmi_open() */
unsigned long long zzhdmi_pos(void);

/* n bytes at stream time at (taken mod the ring); and silence.  After a
   stall (the time played past what was written), zero the whole ring. */
void zzhdmi_write(unsigned long long at, const void *frames, unsigned long n);
void zzhdmi_silence(unsigned long long at, unsigned long n);

/* the stream off, the HDMI ring silent, a server started here ended */
void zzhdmi_close(void);

#endif
