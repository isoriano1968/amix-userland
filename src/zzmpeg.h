/*
 * zzmpeg.h -- MPEG-1 system streams decoded by the Z3660 firmware's ARM
 * (pl_mpeg), shown in the RTG's hardware overlay.  See zzmpeg.c.
 */
#ifndef ZZMPEG_H
#define ZZMPEG_H

/* 0: the board takes the file (it starts the decoder; the overlay comes up
   at x, y); -1: not here, or not an MPEG-1 system stream */
int zzm_open(const char *file, int x, int y, int verbose);
/* feed the board as far as the stream's rate allows; 1 if anything went */
int zzm_pump(void);
/* the picture's size once the ARM has it (0 before), and its media time */
int zzm_size(int *w, int *h);
double zzm_time(void);
/* the stream's real average rate, if lower than its header's */
void zzm_rate(long bytes);
/* 2: the overlay shows the picture twice as tall (a 1920-wide screen) */
int zzm_vscale(void);
/* the overlay's top left corner on the screen */
void zzm_place(int x, int y);
/* the whole file is fed and the picture has stood still since */
int zzm_done(void);
void zzm_close(void);

#endif
