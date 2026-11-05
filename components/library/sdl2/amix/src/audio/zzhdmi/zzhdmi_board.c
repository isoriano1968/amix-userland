/*
  zzhdmi_board.c -- sound on the Z3660's HDMI through core 1's GL server
  (see zzhdmi_board.h).  The board's numbers are amix-gate's:
  firmware/core1 zzgl/zzgl.h and cores/h264bench/bench.h, on the include
  path.  The same steps as libmikmod's drv_hdmi.c: the server is used when
  it runs (a GL program's, alongside it), else started here as a job for
  core 1 and ended again at the close.
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.
*/
#include "zzhdmi_board.h"

#if defined(__amix__) && defined(__m68k__)    /* not pt2-clone's host build */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "bench.h"
#include "zzgl.h"

extern int munmap();                    /* not in AMIX's sys/mman.h */

#if ZZGL_SND_SIZE != ZZHDMI_RING_BYTES
#error "zzhdmi_board.h: ZZHDMI_RING_BYTES differs from ZZGL_SND_SIZE in zzgl.h"
#endif

#define ZZ9KIOC_SETREG      (('Z' << 8) | 7)
struct zz9k_reg { unsigned short offset, value; };

#define FB_PHYS             0x00200000UL
#define PAGE                4096UL
#define REG_AUDIO_PARAM     0x1EC
#define REG_AUDIO_VAL       0x1F0

static int fd = -1, started, div_;
static unsigned long guard;            /* zeroed this far ahead of each write */
static volatile unsigned long *ctl, *blk;
static unsigned char *stream;
static unsigned long last_raw;          /* ZB_SND_POS last read (HDMI bytes, wraps at 4 GB) */
static unsigned long long played;       /* HDMI bytes since the open, unwrapped */

static void *board(unsigned long off, unsigned long len)
{
    void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

    return p == (void *)-1 ? NULL : p;
}

static void unmap(volatile void *p, unsigned long len)
{
    munmap((caddr_t)p, (size_t)len);
}

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

/* core 1 runs the GL server's program: the gpu core, or the amix core */
static int runs_gpu(void)
{
    return ctl[W_MAGIC] == HB_MAGIC_READY &&
           (ctl[W_NAME] == ('g' << 24 | 'p' << 16 | 'u' << 8) ||
            ctl[W_NAME] == ((unsigned long)'a' << 24 | 'm' << 16 | 'i' << 8 | 'x'));
}

/* another program writes the stream: it is on and its write position moves */
static int stream_busy(void)
{
    unsigned long wr;
    int i;

    if (blk[ZB_MAGIC] != ZZGL_MAGIC || blk[ZB_SND_ON] == 0)
        return 0;
    wr = blk[ZB_SND_WR];
    for (i = 0; i < 15; i++) {
        usleep(10000);
        if (blk[ZB_SND_WR] != wr)
            return 1;
    }
    return 0;                           /* left on by a program that ended */
}

/* core 1's server: running already, or started here */
static int server(void)
{
    volatile unsigned long *data;
    int i;

    if (blk[ZB_MAGIC] == ZZGL_MAGIC && ctl[W_STATE] == HB_RUNNING)
        return 0;
    if (ctl[W_STATE] == HB_POSTED || ctl[W_STATE] == HB_RUNNING)
        return -1;                      /* core 1 does another job */
    if (!(data = board(HB_DATA_OFF, PAGE)))
        return -1;
    blk[ZB_MAGIC] = 0;
    data[0] = ZZGL_JOB_MAGIC;
    ctl[W_DATA_OFF] = HB_DATA_OFF;
    ctl[W_DATA_SIZE] = 4;
    ctl[W_FLAGS] = 0;
    ctl[W_SEQ] = ctl[W_SEQ] + 1;
    ctl[W_STATE] = HB_POSTED;
    unmap(data, PAGE);
    for (i = 0; i < 300 && blk[ZB_MAGIC] != ZZGL_MAGIC; i++)
        usleep(10000);
    if (blk[ZB_MAGIC] != ZZGL_MAGIC)
        return -1;
    started = 1;
    return 0;
}

/* a server started here ends here: core 1 is free again */
static void server_end(void)
{
    volatile unsigned long *ring;
    unsigned long wr;

    if (!started || !(ring = board(ZZGL_RING_OFF, ZZGL_RING_SIZE)))
        return;
    wr = blk[ZB_WR];
    if (wr + 16 > ZZGL_RING_SIZE) {
        if (wr + 8 <= ZZGL_RING_SIZE) {
            ring[wr / 4] = ZZ_WRAP;
            ring[wr / 4 + 1] = 0;
        }
        wr = 0;
    }
    ring[wr / 4] = ZZ_CLOSE;
    ring[wr / 4 + 1] = 0;
    blk[ZB_WR] = wr + 8;
    unmap(ring, ZZGL_RING_SIZE);
    started = 0;
}

static void release(void)
{
    if (stream)
        unmap(stream, ZZGL_SND_SIZE);
    if (blk)
        unmap(blk, PAGE);
    if (ctl)
        unmap(ctl, PAGE);
    if (fd >= 0)
        close(fd);
    stream = NULL;
    ctl = blk = NULL;
    fd = -1;
    div_ = 0;
}

int zzhdmi_present(void)
{
    int present = 0;

    if (fd >= 0)
        return 0;                       /* open here already */
    if ((fd = open("/dev/zz9000", O_RDWR)) < 0)
        return 0;
    if ((ctl = board(HB_CTL_OFF, PAGE)) != NULL && runs_gpu() &&
        (blk = board(ZZGL_BLK_OFF, PAGE)) != NULL)
        present = !stream_busy();
    release();
    return present;
}

int zzhdmi_open(int div, unsigned long keep)
{
    const unsigned long margin = 48000UL * 4 / (unsigned long)(div == 1 ? 1 : 2) / 20;  /* 50 ms */

    if (fd >= 0 || (div != 1 && div != 2))
        return -1;
    if ((fd = open("/dev/zz9000", O_RDWR)) < 0)
        return -1;
    ctl = board(HB_CTL_OFF, PAGE);
    blk = board(ZZGL_BLK_OFF, PAGE);
    stream = board(ZZGL_SND_OFF, ZZGL_SND_SIZE);
    if (!ctl || !blk || !stream || !runs_gpu() || server() < 0) {
        release();
        return -1;
    }
    memset(stream, 0, ZZGL_SND_SIZE);
    blk[ZB_SND_ON] = 0;
    blk[ZB_SND_WR] = 0;
    reg32(REG_AUDIO_PARAM, 0);
    reg32(REG_AUDIO_VAL, ZZGL_HDMI_OFF);
    usleep(100000);                     /* core 0's main loop takes it */
    blk[ZB_SND_POS] = 0;
    last_raw = 0;
    played = 0;
    div_ = div;
    guard = keep + margin < ZZGL_SND_SIZE ? ZZGL_SND_SIZE - keep - margin : 0;
    blk[ZB_SND_ON] = (unsigned long)div;
    return 0;
}

unsigned long long zzhdmi_pos(void)
{
    unsigned long raw;

    if (!blk || !div_)
        return 0;
    raw = blk[ZB_SND_POS];
    played += (unsigned long)(raw - last_raw);  /* mod 2^32: also across its wrap */
    last_raw = raw;
    return played / (unsigned long long)div_;
}

void zzhdmi_write(unsigned long long at, const void *frames, unsigned long n)
{
    unsigned long off, first;

    if (!stream || !n || n > ZZGL_SND_SIZE)
        return;
    off = (unsigned long)(at % ZZGL_SND_SIZE);
    first = ZZGL_SND_SIZE - off;
    if (first > n)
        first = n;
    memcpy(stream + off, frames, first);
    if (n > first)
        memcpy(stream, (const char *)frames + first, n - first);
    blk[ZB_SND_WR] = (unsigned long)(at + n);
    /* a lap ahead minus what may still be heard: silence, so that core 1
       plays silence, not the last lap, when the writer stalls */
    if (guard >= n)
        zzhdmi_silence(at + guard, n);
}

void zzhdmi_silence(unsigned long long at, unsigned long n)
{
    unsigned long off, first;

    if (!stream || !n)
        return;
    if (n > ZZGL_SND_SIZE)
        n = ZZGL_SND_SIZE;
    off = (unsigned long)(at % ZZGL_SND_SIZE);
    first = ZZGL_SND_SIZE - off;
    if (first > n)
        first = n;
    memset(stream + off, 0, first);
    if (n > first)
        memset(stream, 0, n - first);
}

void zzhdmi_close(void)
{
    unsigned char *ring;

    if (fd < 0)
        return;
    if (blk) {
        blk[ZB_SND_ON] = 0;
        usleep(20000);                  /* core 1 lets go of the ring */
        if ((ring = board(ZZGL_HDMI_OFF, ZZGL_HDMI_SIZE)) != NULL) {
            /* the DMA loops it for ever: silence, not the last notes */
            memset(ring, 0, ZZGL_HDMI_SIZE);
            unmap(ring, ZZGL_HDMI_SIZE);
        }
        server_end();
    }
    release();
}

#else /* not AMIX: no board (pt2-clone's host build) */

int zzhdmi_present(void) { return 0; }
int zzhdmi_open(int div, unsigned long keep) { (void)div; (void)keep; return -1; }
unsigned long long zzhdmi_pos(void) { return 0; }
void zzhdmi_write(unsigned long long at, const void *frames, unsigned long n) { (void)at; (void)frames; (void)n; }
void zzhdmi_silence(unsigned long long at, unsigned long n) { (void)at; (void)n; }
void zzhdmi_close(void) { }

#endif
