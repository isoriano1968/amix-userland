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

/* core 1 fetches and demuxes the stream itself (the amix core with
   ZZAV_FEAT_URL and the firmware with core 1's network): 0 opened, the
   picture's size in *w x *h (0 x 0: sound only); -1 not here (play it as
   before); -2 core 1 tried and failed (said why) */
int zzav_url_open(const char *url, double start, int *w, int *h, int verbose);
/* then as zzav_open's x, y, maxw, maxh: the overlay there, playing */
int zzav_url_play(int x, int y, int maxw, int maxh);
/* t s after the stream's start (core 1 seeks, or fetches anew from there) */
int zzav_url_seek(double t);
/* opened, not played: core 1 back to idle */
void zzav_url_cancel(void);
/* 1 if core 1 played it to its end */
int zzav_ended(void);
/* its length (0 live), its first time stamp, 1 if the server seeks */
void zzav_url_info(double *dur, double *t0, int *seekable);
/* seconds demuxed beyond the clock, the download's KB/s, KB in all */
void zzav_url_net(double *buffered, long *kbs, long *kb);

#endif
