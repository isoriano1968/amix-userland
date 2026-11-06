/*
 * zzav.c -- H.264 (+ AAC) played by the Z3660's second ARM core: vplay
 * demuxes on the 68060 and puts the packets into a ring in board memory;
 * core 1 (arm/h264bench/player.c, in the amix core: `armbench -reload z3660ARM_Core_amix.bin`;
 * z3660ARM_Core_h264bench.bin has the player alone)
 * decodes them, writes the pictures into the RTG's overlay and the sound
 * into the board's HDMI ring.  The interface is arm/h264bench/zzav.h.
 *
 * The overlay is switched on the way zzmpeg.c does it (the firmware's MPEG
 * INIT command, then its position and size; HEIGHT re-enables it), with the
 * same 1080p trap: the overlay's DMA fetches a buffer row every second
 * screen line, so the picture is written at row y / vs and shown vs times
 * as tall.  The firmware's MPEG decoder gets no data: it never runs.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <libavformat/avformat.h>
#include "../arm/h264bench/zzav.h"
#include "../arm/h264bench/bench.h"
#include "zzav.h"

extern int munmap();                    /* not in AMIX's sys/mman.h */

/* the AMIX zz9000 driver (zz9000.h), as zzmpeg.c */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };
#define FB_PHYS             0x00200000UL

#define REG_AUDIO_PARAM     0x1EC
#define REG_AUDIO_VAL       0x1F0
#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define REG_OVL_YSTART      0x32C
#define REG_OVL_XSTART      0x330
#define REG_OVL_WIDTH       0x334
#define REG_OVL_HEIGHT      0x338
#define CMD_INIT            1
#define CMD_CLOSE           5
/* the MPEG decoder's FIFO and shared block, as zzmpeg.c; a small FIFO: its
   last pages hold core 0's loader and core 1's blocks */
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00010000UL
#define SHARED_OFF          0x07B00000UL
#define PAGE                4096UL

static int fd = -1, opened, vs = 1, ovl_w, ovl_h, outw, outh, shift, verbose, vid_w, vid_h, cur_x, cur_y;
static int disp_w;                      /* vid_w times the sample aspect ratio: the width shown */
static volatile unsigned long *blk, *c1ld;
static unsigned long board_base;
static unsigned char *ring, *shared;
static unsigned long wr;
static int vidx = -1, aidx = -1;
const char *zzav_prog = "vplay";        /* the messages' prefix (ffplay sets its own) */

static void reg32(unsigned off, unsigned long v)
{
    struct zz9k_reg r;

    r.offset = off;
    r.value = (unsigned short)(v >> 16);
    ioctl(fd, ZZ9KIOC_SETREG, &r);
    r.offset = off + 2;
    r.value = (unsigned short)v;
    ioctl(fd, ZZ9KIOC_SETREG, &r);
}

static void *map(unsigned long off, unsigned long len)
{
    void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

    return p == (void *)-1 ? NULL : p;
}

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* the picture's size in the overlay for the current shift, cropped to the screen */
static int zzav_reopen;                 /* opened before: say why not */
static int zscale = 1;                  /* the picture on screen: its pixels this many times */
static void set_out(void)
{
    outw = ((disp_w >> shift) & ~1) * zscale;
    outh = ((vid_h >> shift) & ~1) * zscale;
    if (outw > ovl_w)
        outw = ovl_w;
    if (outh * vs > ovl_h)
        outh = ovl_h / vs;
}

/* /dev/zz9000, the blocks mapped, core 1's player there and idle; ovl_w, ovl_h, vs */
static int attach(void)
{
    struct zz9k_info info;
    double t0;

    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0)
        return -1;
    board_base = info.base;
    blk = (volatile unsigned long *)map(ZZAV_OFF, PAGE);
    ring = (unsigned char *)map(ZZAV_RING_OFF, ZZAV_AREA_SIZE);
    shared = (unsigned char *)map(SHARED_OFF, PAGE);
    c1ld = (volatile unsigned long *)map(C1LD_OFF, PAGE);
    if (!blk || !ring || !shared || !c1ld)
        return -1;
    /* a stream left by a player that is gone (killed, crashed, closed with
       the window): nobody will CLOSE it but us, and until then every new
       player falls back to the 060.  READY, ERROR and FINISHED are always
       left over; PLAYING and OPENING are when nothing moves for 1.5 s -- no
       packets written, no media time -- and the stream is not paused (a
       paused player of someone else's is left alone). */
    if (blk[ZZAV_W_MAGIC] == ZZAV_MAGIC && !blk[ZZAV_W_CMD] && blk[ZZAV_W_STATE] != ZZAV_IDLE) {
        unsigned long st = blk[ZZAV_W_STATE];

        if (st == ZZAV_READY || st == ZZAV_ERROR || st == ZZAV_FINISHED)
            blk[ZZAV_W_CMD] = ZZAV_CMD_CLOSE;
        else if (!blk[ZZAV_W_PAUSE]) {
            unsigned long wr = blk[ZZAV_W_WR], clk = blk[ZZAV_W_CLOCK_MS];
            int moved = 0;

            for (t0 = now(); now() - t0 < 1.5 && !moved; usleep(50000))
                moved = blk[ZZAV_W_WR] != wr || blk[ZZAV_W_CLOCK_MS] != clk ||
                        blk[ZZAV_W_STATE] != st || blk[ZZAV_W_CMD] || blk[ZZAV_W_PAUSE];
            if (!moved) {
                if (verbose)
                    fprintf(stderr, "%s: closing a stream left on core 1 (state %lu)\n", zzav_prog, st);
                blk[ZZAV_W_CMD] = ZZAV_CMD_CLOSE;
            }
        }
    }
    /* a seek opens it again at once: core 1 may still be closing the last one */
    for (t0 = now(); blk[ZZAV_W_MAGIC] == ZZAV_MAGIC && (blk[ZZAV_W_STATE] != ZZAV_IDLE || blk[ZZAV_W_CMD]) &&
         now() - t0 < 3; )
        usleep(10000);
    if (blk[ZZAV_W_MAGIC] != ZZAV_MAGIC || blk[ZZAV_W_STATE] != ZZAV_IDLE || blk[ZZAV_W_CMD]) {
        if (verbose || zzav_reopen)
            fprintf(stderr, "%s: no H.264 player on the Z3660's core 1 (magic %lx state %lu cmd %lu; "
                    "run armbench -reload z3660ARM_Core_amix.bin)\n", zzav_prog,
                    blk[ZZAV_W_MAGIC], blk[ZZAV_W_STATE], blk[ZZAV_W_CMD]);
        return -1;
    }
    ovl_w = info.width;
    ovl_h = info.height;
    /* older firmware shows each overlay row twice at 1920 wide; with the fix
       (C1LD_FEAT_OVL_FETCH in core 0's loader block) the overlay is 1:1 */
    vs = ovl_w >= 1600 && !(c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FETCH)) ? 2 : 1;
    if (getenv("ZZM_VSCALE"))
        vs = atoi(getenv("ZZM_VSCALE")) == 2 ? 2 : 1;
    return 0;
}

static void detach(void)
{
    if (blk)
        munmap((caddr_t)blk, (size_t)PAGE);
    if (ring)
        munmap((caddr_t)ring, (size_t)ZZAV_AREA_SIZE);
    if (c1ld)
        munmap((caddr_t)c1ld, (size_t)PAGE);
    if (shared)
        munmap((caddr_t)shared, (size_t)PAGE);
    blk = c1ld = NULL;
    ring = shared = NULL;
    if (fd >= 0)
        close(fd);
    fd = -1;
}

/* the picture w x h with sample aspect ratio sar: the width shown, the shift, the size out */
static void geometry(int w, int h, AVRational sar, int maxw, int maxh)
{
    /* the width shown: the coded one times the sample aspect ratio (a 16:9
       DVD is 720 x 576 with pixels 64:45 wide), as ffplay and VLC show it */
    disp_w = w;
    if (sar.num > 0 && sar.den > 0 && sar.num != sar.den)
        disp_w = (int)(((long long)w * sar.num + sar.den / 2) / sar.den) & ~1;
    if (disp_w < 2 || disp_w > 4 * w)
        disp_w = w;
    /* 1:1 if it fits the screen (and the window, when one is given), else half size */
    shift = disp_w > ovl_w || h * vs > ovl_h ||
            (maxw > 0 && disp_w > maxw) || (maxh > 0 && h * vs > maxh);
    if (getenv("ZZAV_SHIFT"))
        shift = atoi(getenv("ZZAV_SHIFT")) ? 1 : 0;
    vid_w = w;
    vid_h = h;
    set_out();
}

/* the overlay words, the overlay on, the sound DMA, OPEN: core 1 plays */
static int start(int sound, int x, int y)
{
    double t0;
    int i;

    blk[ZZAV_W_DISPW] = disp_w != vid_w ? (unsigned long)disp_w : 0;
    blk[ZZAV_W_OVL_PITCH] = (unsigned long)ovl_w * 4;
    blk[ZZAV_W_OVL_SHIFT] = (unsigned long)shift;
    blk[ZZAV_W_OVL_SCALE] = (unsigned long)zscale;
    blk[ZZAV_W_OVL_MAXW] = (unsigned long)outw;
    blk[ZZAV_W_OVL_MAXH] = (unsigned long)outh;
    blk[ZZAV_W_VOLUME] = 256;
    blk[ZZAV_W_PAUSE] = 0;
    blk[ZZAV_W_OVL_DBUF] = c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FLIP) &&
                           !getenv("ZZAV_NODBUF");

    /* the overlay on: the firmware's MPEG INIT (with nothing to decode), then place and size */
    for (i = 0; i < 96; i++)
        shared[i] = 0;
    reg32(REG_MPEG_PARAM0 + 0, board_base + SHARED_OFF);
    reg32(REG_MPEG_PARAM0 + 4, board_base);
    reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(REG_MPEG_CMD, CMD_INIT);
    opened = 1;
    zzav_place(x, y);

    /* the board's sound DMA: off after boot (AMIX has no AHI driver to start it); the ring's
       address in REG_ZZ_AUDIO_PARAM 0 / VAL makes core 0 clear the ring and start it */
    if (sound) {
        reg32(REG_AUDIO_PARAM, 0);
        reg32(REG_AUDIO_VAL, ZZAV_AUDIO_OFF);
        usleep(100000);                 /* core 0's main loop takes it */
    }
    blk[ZZAV_W_CMD] = ZZAV_CMD_OPEN;
    for (t0 = now(); blk[ZZAV_W_CMD] == ZZAV_CMD_OPEN && now() - t0 < 2; )
        usleep(10000);
    if (blk[ZZAV_W_CMD] || blk[ZZAV_W_STATE] != ZZAV_PLAYING) {
        fprintf(stderr, "%s: core 1's player did not start (state %lu, error %lu)\n", zzav_prog,
                blk[ZZAV_W_STATE], blk[ZZAV_W_ERR]);
        zzav_close();
        return -1;
    }
    if (verbose)
        fprintf(stderr, "%s: H.264 %dx%d%s on the Z3660's core 1, shown %dx%d%s%s\n", zzav_prog,
                vid_w, vid_h, sound ? " + AAC" : "", outw, outh * vs, shift ? " (half size)" : "",
                blk[ZZAV_W_OVL_DBUF] ? ", double-buffered" : "");
    return 0;
}

/* the player's code for a sound codec: AAC, and with ZZAV_FEAT_SOUND (the amix
   core since 2026-11-05) AC3, E-AC3, DTS, MP3, MP2, Vorbis; ZZAV_NONE if not */
static unsigned long acode(enum AVCodecID id, int more)
{
    if (id == AV_CODEC_ID_AAC)
        return ZZAV_AAC;
    if (!more)
        return ZZAV_NONE;
    switch (id) {
    case AV_CODEC_ID_AC3: return ZZAV_AC3;
    case AV_CODEC_ID_EAC3: return ZZAV_EAC3;
    case AV_CODEC_ID_DTS: return ZZAV_DTS;
    case AV_CODEC_ID_MP3: return ZZAV_MP3;
    case AV_CODEC_ID_MP2: return ZZAV_MP2;
    case AV_CODEC_ID_VORBIS: return ZZAV_VORBIS;
    default: return ZZAV_NONE;
    }
}

int zzav_open(AVFormatContext *fmt, int v, int a, int x, int y, int maxw, int maxh, int verb)
{
    AVCodecParameters *vc = v >= 0 ? fmt->streams[v]->codecpar : NULL;
    AVCodecParameters *ac = a >= 0 ? fmt->streams[a]->codecpar : NULL;
    int i;

    verbose = verb;
    if (getenv("VPLAY_NOARM") || !vc || vc->codec_id != AV_CODEC_ID_H264 ||
        (vc->format != AV_PIX_FMT_YUV420P && vc->format != AV_PIX_FMT_YUVJ420P && vc->format != -1) ||
        vc->extradata_size > (int)ZZAV_EXTRA_MAX)
        return -1;
    if (ac && ac->extradata_size > (int)ZZAV_EXTRA_MAX)
        ac = NULL, a = -1;
    if (attach() < 0)
        goto fail;
    /* the sound core 1 decodes: AAC, or more with an amix core that says so */
    if (ac && acode(ac->codec_id, (blk[ZZAV_W_FEAT] & ZZAV_FEAT_SOUND) != 0) == ZZAV_NONE)
        ac = NULL, a = -1;
    geometry(vc->width, vc->height, av_guess_sample_aspect_ratio(fmt, fmt->streams[v], NULL), maxw, maxh);

    /* the stream words; not FEAT and what follows: core 1's (OPEN_URL) */
    for (i = ZZAV_W_VCODEC; i < ZZAV_W_FEAT; i++)
        blk[i] = 0;
    blk[ZZAV_W_WR] = blk[ZZAV_W_RD] = 0;
    wr = 0;
    blk[ZZAV_W_VCODEC] = ZZAV_H264;
    memcpy(ring + (ZZAV_VEXTRA_OFF - ZZAV_RING_OFF), vc->extradata, (size_t)vc->extradata_size);
    blk[ZZAV_W_VEXTRA_LEN] = (unsigned long)vc->extradata_size;
    blk[ZZAV_W_VTB_NUM] = (unsigned long)fmt->streams[v]->time_base.num;
    blk[ZZAV_W_VTB_DEN] = (unsigned long)fmt->streams[v]->time_base.den;
    blk[ZZAV_W_VW] = (unsigned long)vc->width;
    blk[ZZAV_W_VH] = (unsigned long)vc->height;
    if (ac) {
        blk[ZZAV_W_ACODEC] = acode(ac->codec_id, 1);
        memcpy(ring + (ZZAV_AEXTRA_OFF - ZZAV_RING_OFF), ac->extradata, (size_t)ac->extradata_size);
        blk[ZZAV_W_AEXTRA_LEN] = (unsigned long)ac->extradata_size;
        blk[ZZAV_W_ATB_NUM] = (unsigned long)fmt->streams[a]->time_base.num;
        blk[ZZAV_W_ATB_DEN] = (unsigned long)fmt->streams[a]->time_base.den;
        blk[ZZAV_W_ARATE] = (unsigned long)ac->sample_rate;
        blk[ZZAV_W_ACH] = (unsigned long)ac->channels;
    }
    vidx = v;
    aidx = a;
    return start(ac != NULL, x, y);
fail:
    if (zzav_reopen)
        fprintf(stderr, "%s: core 1 not opened again: fd %d, errno %d\n", zzav_prog, fd, errno);
    detach();
    return -1;
}

/* ---- streams core 1 fetches itself (ZZAV_CMD_OPEN_URL) ---- */

static char url_cur[ZZAV_URL_MAX];
static int url_maxw, url_maxh;

static const char *neterr(unsigned long e)
{
    switch (e) {
    case ZZAV_NETERR_URL: return "not an http:// URL";
    case ZZAV_NETERR_NONET: return "no network on core 1 (no DHCP address, or the firmware has no core 1 rings)";
    case ZZAV_NETERR_DNS: return "the host's name did not resolve";
    case ZZAV_NETERR_CONNECT: return "no connection";
    case ZZAV_NETERR_HTTP: return "the server said no";
    case ZZAV_NETERR_FORMAT: return "not a format core 1 knows (MPEG-TS, MP4, MKV, FLV, AVI)";
    case ZZAV_NETERR_STREAMS: return "no H.264 picture and no AAC sound in it";
    case ZZAV_NETERR_TIMEOUT: return "the server stopped answering";
    case ZZAV_NETERR_MEM: return "core 1 is out of memory";
    }
    return "?";
}

/* OPEN_URL and its answer; 0 READY, -1 not taken (a core without it), -2 failed (said why) */
static int url_ask(double start)
{
    double t0;

    blk[ZZAV_W_START_MS] = start > 0 ? (unsigned long)(start * 1000) : 0;
    blk[ZZAV_W_CMD] = ZZAV_CMD_OPEN_URL;
    /* taken at once by a core that has it; an older one never takes it */
    for (t0 = now(); blk[ZZAV_W_CMD] == ZZAV_CMD_OPEN_URL && now() - t0 < 2; )
        usleep(10000);
    if (blk[ZZAV_W_CMD] == ZZAV_CMD_OPEN_URL) {
        blk[ZZAV_W_CMD] = 0;
        return -1;
    }
    for (t0 = now(); now() - t0 < 60; ) {
        unsigned long st = blk[ZZAV_W_STATE];

        if (!blk[ZZAV_W_CMD] && (st == ZZAV_READY || st == ZZAV_ERROR || st == ZZAV_IDLE))
            break;
        usleep(20000);
    }
    if (blk[ZZAV_W_STATE] == ZZAV_READY)
        return 0;
    if (blk[ZZAV_W_STATE] == ZZAV_ERROR)
        fprintf(stderr, "%s: core 1 cannot play it: %s (error %lx, HTTP %lu)\n", zzav_prog,
                neterr(blk[ZZAV_W_ERR]), blk[ZZAV_W_ERR], blk[ZZAV_W_HTTP]);
    else
        fprintf(stderr, "%s: core 1 did not open it in 60 s (state %lu)\n", zzav_prog, blk[ZZAV_W_STATE]);
    blk[ZZAV_W_CMD] = ZZAV_CMD_CLOSE;           /* back to IDLE */
    return -2;
}

int zzav_url_open(const char *url, double start, int *w, int *h, int verb)
{
    volatile unsigned long *ctl;
    int r;

    verbose = verb;
    if (getenv("VPLAY_NOARM") || strncmp(url, "http://", 7) || strlen(url) >= ZZAV_URL_MAX)
        return -1;
    if (attach() < 0) {
        detach();
        return -1;
    }
    /* the amix core with OPEN_URL (an older core leaves FEAT as it was: ask its name too) */
    ctl = (volatile unsigned long *)map(HB_CTL_OFF, PAGE);
    if (!ctl || !(blk[ZZAV_W_FEAT] & ZZAV_FEAT_URL) || memcmp((const char *)&ctl[W_NAME], "amix", 4)) {
        if (ctl)
            munmap((caddr_t)ctl, (size_t)PAGE);
        detach();
        return -1;
    }
    munmap((caddr_t)ctl, (size_t)PAGE);
    strcpy(url_cur, url);
    {
        /* the URL's page is the player's block: ZZAV_URL_OFF is inside it */
        volatile char *u = (volatile char *)blk + (ZZAV_URL_OFF - ZZAV_OFF);
        size_t i;

        for (i = 0; i <= strlen(url); i++)
            u[i] = url[i];
    }
    if ((r = url_ask(start)) < 0) {
        detach();
        return r;
    }
    vidx = blk[ZZAV_W_VCODEC] == ZZAV_H264 ? 0 : -1;
    aidx = blk[ZZAV_W_ACODEC] == ZZAV_AAC ? 1 : -1;
    *w = vidx >= 0 ? (int)blk[ZZAV_W_VW] : 0;
    *h = vidx >= 0 ? (int)blk[ZZAV_W_VH] : 0;
    return 0;
}

int zzav_url_play(int x, int y, int maxw, int maxh)
{
    AVRational sar;

    sar.num = (int)blk[ZZAV_W_SAR_NUM];
    sar.den = (int)blk[ZZAV_W_SAR_DEN];
    url_maxw = maxw;
    url_maxh = maxh;
    geometry(vidx >= 0 ? (int)blk[ZZAV_W_VW] : 2, vidx >= 0 ? (int)blk[ZZAV_W_VH] : 2, sar, maxw, maxh);
    return start(aidx >= 0, x, y);
}

/* the same stream from t s after its start: core 1 seeks (or fetches anew), the player starts again */
int zzav_url_seek(double t)
{
    double t0;

    if (!opened)
        return -1;
    blk[ZZAV_W_CMD] = ZZAV_CMD_CLOSE;
    for (t0 = now(); (blk[ZZAV_W_CMD] || blk[ZZAV_W_STATE] != ZZAV_IDLE) && now() - t0 < 3; )
        usleep(10000);
    if (url_ask(t < 0 ? 0 : t) < 0)
        return -1;
    blk[ZZAV_W_CMD] = ZZAV_CMD_OPEN;
    for (t0 = now(); blk[ZZAV_W_CMD] == ZZAV_CMD_OPEN && now() - t0 < 2; )
        usleep(10000);
    return blk[ZZAV_W_STATE] == ZZAV_PLAYING ? 0 : -1;
}

/* opened but not played (it has no picture, say): core 1 back to idle */
void zzav_url_cancel(void)
{
    double t0;

    if (!blk || opened)
        return;
    blk[ZZAV_W_CMD] = ZZAV_CMD_CLOSE;
    for (t0 = now(); blk[ZZAV_W_CMD] && now() - t0 < 2; )
        usleep(10000);
    detach();
}

/* 1 if core 1 played it to its end (not stopped by an error) */
int zzav_ended(void)
{
    return opened && blk[ZZAV_W_STATE] == ZZAV_FINISHED;
}

void zzav_url_info(double *dur, double *t0, int *seekable)
{
    *dur = blk ? blk[ZZAV_W_DUR_MS] / 1000.0 : 0;
    *t0 = blk ? blk[ZZAV_W_T0_MS] / 1000.0 : 0;
    *seekable = blk ? (int)blk[ZZAV_W_SEEKABLE] : 0;
}

/* buffered seconds beyond the clock, the download's KB/s, KB in all */
void zzav_url_net(double *buffered, long *kbs, long *kb)
{
    *buffered = blk ? blk[ZZAV_W_BUF_MS] / 1000.0 : 0;
    *kbs = blk ? (long)blk[ZZAV_W_NET_KBS] : 0;
    *kb = blk ? (long)blk[ZZAV_W_NET_KB] : 0;
}

int zzav_stream(int idx)
{
    return opened && idx >= 0 && (idx == vidx || idx == aidx);
}

/* one packet into the ring; 0: no room now, try again later */
int zzav_send(const AVPacket *p)
{
    unsigned long need, rd, room;
    unsigned long h[4];
    int type;

    if (!opened)
        return 1;
    if (p->stream_index != vidx && p->stream_index != aidx)
        return 1;
    type = p->stream_index == vidx ? ZZAV_PKT_VIDEO : ZZAV_PKT_AUDIO;
    need = (ZZAV_PKT_HDR + (unsigned long)p->size + 3) & ~3UL;
    if (need >= ZZAV_RING_SIZE / 2)
        return 1;                       /* cannot be: drop it */
    rd = blk[ZZAV_W_RD];
    if (wr + need > ZZAV_RING_SIZE) {   /* no room before the end: wrap */
        if (rd > wr || rd == 0)
            return 0;
        *(volatile unsigned long *)(ring + wr) = (unsigned long)ZZAV_PKT_WRAP << 24;
        wr = 0;
        blk[ZZAV_W_WR] = wr;
    }
    room = rd > wr ? rd - wr - 1 : ZZAV_RING_SIZE - wr + rd - 1;
    if (need > room)
        return 0;
    memcpy(ring + wr + ZZAV_PKT_HDR, p->data, (size_t)p->size);
    h[0] = (unsigned long)type << 24 | (p->flags & AV_PKT_FLAG_KEY ? ZZAV_PKT_KEY : 0);
    h[1] = (unsigned long)p->size;
    if (p->pts == AV_NOPTS_VALUE) {
        h[2] = 0x80000000UL;
        h[3] = 0;
    } else {
        h[2] = (unsigned long)((uint64_t)p->pts >> 32);
        h[3] = (unsigned long)p->pts;
    }
    memcpy(ring + wr, h, ZZAV_PKT_HDR);
    wr += need;
    if (wr >= ZZAV_RING_SIZE)
        wr = 0;
    blk[ZZAV_W_WR] = wr;
    return 1;
}

void zzav_eof(void)
{
    if (opened)
        blk[ZZAV_W_EOF] = 1;
}

void zzav_pause(int on)
{
    if (opened)
        blk[ZZAV_W_PAUSE] = on ? 1 : 0;
}

void zzav_volume(int v)
{
    if (opened)
        blk[ZZAV_W_VOLUME] = (unsigned long)(v < 0 ? 0 : v > 256 ? 256 : v);
}

void zzav_size(int *w, int *h, int *vscale)
{
    *w = outw;
    *h = outh;
    *vscale = vs;
}

/* full (0) or half (1) size while playing: core 1 takes the shift with the next frame */
int zzav_half(int half)
{
    if (!opened)
        return 0;
    if (!half && (disp_w > ovl_w || vid_h * vs > ovl_h))
        half = 1;                       /* full size does not fit the screen */
    shift = half ? 1 : 0;
    zscale = 1;
    blk[ZZAV_W_OVL_SCALE] = 1;
    set_out();
    blk[ZZAV_W_OVL_MAXW] = (unsigned long)outw;
    blk[ZZAV_W_OVL_MAXH] = (unsigned long)outh;
    blk[ZZAV_W_OVL_SHIFT] = (unsigned long)shift;
    zzav_place(cur_x, cur_y);
    return shift;
}

/* each pixel s times, as many as fit the screen, 1..4: the overlay does not scale */
void zzav_scale(int s)
{
    int lim;

    if (!opened)
        return;
    if (s > 4)
        s = 4;
    /* a full 1920 pixel line is more than the overlay can fetch in one line
       of a 1080p screen (docs/z3660-overlay-1080p-issue.md) */
    lim = getenv("ZZAV_MAXW") ? atoi(getenv("ZZAV_MAXW")) : 1600;
    while (s > 1 && (disp_w * s > ovl_w || disp_w * s > lim || vid_h * vs * s > ovl_h))
        s--;
    zscale = s < 1 ? 1 : s;
    if (zscale > 1)
        shift = 0;
    set_out();
    blk[ZZAV_W_OVL_MAXW] = (unsigned long)outw;
    blk[ZZAV_W_OVL_MAXH] = (unsigned long)outh;
    blk[ZZAV_W_OVL_SHIFT] = (unsigned long)shift;
    blk[ZZAV_W_OVL_SCALE] = (unsigned long)zscale;
    zzav_place(cur_x, cur_y);
}

/* the overlay's top left corner on the screen */
void zzav_place(int x, int y)
{
    int oy;

    if (!opened)
        return;
    cur_x = x;
    cur_y = y;
    if (x < 0)
        x = 0;
    if (x + outw > ovl_w)
        x = ovl_w - outw;
    oy = y < 0 ? 0 : y;
    if (oy + outh * vs > ovl_h)
        oy = ovl_h - outh * vs;
    blk[ZZAV_W_OVL_ROW0] = (unsigned long)(oy / vs);
    reg32(REG_OVL_XSTART, (unsigned long)x);
    reg32(REG_OVL_YSTART, (unsigned long)oy);
    reg32(REG_OVL_WIDTH, (unsigned long)outw);
    reg32(REG_OVL_HEIGHT, (unsigned long)(outh * vs));     /* takes the new place */
}

/* 1 when everything sent was shown and heard (or core 1 gave up) */
int zzav_done(void)
{
    if (!opened)
        return 1;
    return blk[ZZAV_W_STATE] == ZZAV_FINISHED || blk[ZZAV_W_STATE] == ZZAV_ERROR;
}

void zzav_counts(long *shown, long *dropped)
{
    if (opened) {
        *shown = (long)blk[ZZAV_W_SHOWN];
        *dropped = (long)blk[ZZAV_W_DROPPED];
    }
}

double zzav_clock(void)
{
    return opened ? blk[ZZAV_W_CLOCK_MS] / 1000.0 : 0;
}

void zzav_stats(char *buf, int len)
{
    if (!opened) {
        buf[0] = 0;
        return;
    }
    snprintf(buf, (size_t)len, "core 1: %.1f s (%s clock), %lu shown %lu dropped, decode %lu us, show %lu us, lag %ld ms, skip %lu; "
            "sound %lu ms ahead, %lu gaps, %lu pkts %lu frames %lu samples, err %ld, dma %lu (%lu back); "
            "video pkts %lu%s %lx; refresh %lu us, frames on screen for 1/2/3/4+ refreshes %lu/%lu/%lu/%lu",
            blk[ZZAV_W_CLOCK_MS] / 1000.0, blk[ZZAV_W_CLOCKSRC] == 2 ? "timer" : "sound",
            blk[ZZAV_W_SHOWN], blk[ZZAV_W_DROPPED], blk[ZZAV_W_VDEC_US], blk[ZZAV_W_SHOW_US],
            (long)blk[ZZAV_W_LAG_MS], blk[ZZAV_W_SKIP], blk[ZZAV_W_ALEAD_MS], blk[ZZAV_W_UNDERRUNS],
            blk[ZZAV_W_APKTS], blk[ZZAV_W_AFRAMES], blk[ZZAV_W_ASAMPLES], (long)blk[ZZAV_W_AERR],
            blk[ZZAV_W_DMA_RAW], blk[ZZAV_W_DMA_JUMPS], blk[ZZAV_W_VPKTS],
            blk[ZZAV_W_STATE] == ZZAV_ERROR ? ", ERROR" : "", blk[ZZAV_W_ERR], blk[ZZAV_W_REFRESH_US],
            blk[ZZAV_W_CAD1], blk[ZZAV_W_CAD2], blk[ZZAV_W_CAD3], blk[ZZAV_W_CAD4]);
}

void zzav_close(void)
{
    double t0;

    if (!opened)
        return;
    opened = 0;
    zzav_reopen = 1;
    blk[ZZAV_W_CMD] = ZZAV_CMD_CLOSE;
    for (t0 = now(); blk[ZZAV_W_CMD] == ZZAV_CMD_CLOSE && now() - t0 < 2; )
        usleep(10000);
    reg32(REG_MPEG_CMD, CMD_CLOSE);     /* the overlay off */
    detach();
}
