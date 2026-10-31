/*
 * zzav.h -- H.264 (+ AAC) decoded by the Z3660's second ARM core, shown in
 * the RTG's overlay, heard on the board's HDMI.  See zzav.c.
 */
#ifndef VPLAY_ZZAV_H
#define VPLAY_ZZAV_H

#include <libavformat/avformat.h>

/* 0: core 1 plays streams v (H.264) and a (AAC, or -1), the overlay's top
   left at x, y, at most maxw x maxh (0: the screen); -1: not here */
int zzav_open(AVFormatContext *fmt, int v, int a, int x, int y, int maxw, int maxh, int verbose);
/* 1 if packets of this stream go to core 1 */
int zzav_stream(int idx);
/* hand a packet over: 0 if the ring is full now (try again) */
int zzav_send(const AVPacket *p);
void zzav_eof(void);
void zzav_scale(int s);
void zzav_pause(int on);
void zzav_volume(int v);                /* 0..256 */
/* the picture as the overlay shows it: w x h, vscale times as tall */
void zzav_size(int *w, int *h, int *vscale);
void zzav_place(int x, int y);
/* full (0) or half (1) size while playing; returns the shift in use */
int zzav_half(int half);
int zzav_done(void);
double zzav_clock(void);
void zzav_stats(char *buf, int len);
void zzav_counts(long *shown, long *dropped);
void zzav_close(void);

#endif
