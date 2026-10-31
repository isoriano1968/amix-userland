/*
 * zzmpeg.c -- MPEG-1 system streams (.mpg: MPEG-1 video + MP2) decoded by
 * the Z3660 firmware's own decoder: the ARM decodes, shows the picture in
 * the RTG's hardware overlay (1:1, it does not scale) and plays the sound on
 * the board's HDMI audio; the 68060 only copies the file into a FIFO in
 * board memory.  vplay leaves the sound to the board, or with -paula
 * decodes it itself for Paula.
 *
 * The firmware side (shanshe/Z3660, v1.03 beta 21: mpg/z3660_mpeg_handler.c,
 * mpg/pl_mpeg_arm/pl_mpeg_handler.c, rtg/rtg.c):
 *   - REG_ZZ_MPEG_PARAM0..4 (0x2C8..0x2D8), then a command in
 *     REG_ZZ_MPEG_CMD_OP (0x2C0): INIT 1 (PARAM0 the 68k address of a
 *     shared block, PARAM1 the board's 68k address, PARAM2 the FIFO size,
 *     PARAM3 its offset in board memory), PROCESS 2 (take what the FIFO
 *     has up to the write index), CLOSE 5 (also turns the overlay off);
 *   - REG_ZZ_MPEG_FIFOTX (0x2E4): the FIFO's write index;
 *   - the overlay's position REG_ZZ_OVERLAY_XSTART/YSTART (0x330/0x32C)
 *     and size WIDTH/HEIGHT (0x334/0x338; HEIGHT re-enables it);
 *   - the shared block (arm_decoder_shared_arm_t): big-endian values laid
 *     out by the ARM's compiler (doubles 8-aligned), read here by offset.
 * Traps:
 *   - the first command must be INIT: every command writes its status
 *     through the block's pointer, which is NULL until INIT;
 *   - every register write waits until the ARM's main loop gets to it,
 *     which it does between frames: few, big writes (256 KB a feed);
 *   - the firmware's own ring is 1 MB and takes data on in 512 KB steps,
 *     and the decoder's input buffer grows without bound: the file is fed
 *     at the stream's rate (the pack header's mux_rate), AHEAD bytes ahead
 *     of the video time the ARM reports; that time is rewritten by PROCESS,
 *     so the largest value read counts;
 *   - the overlay's place on the screen is taken by the hardware when the
 *     overlay is set up (INIT, or a write of HEIGHT); the ARM writes each
 *     frame YSTART rows down into the overlay buffer, whose row k the
 *     hardware shows on screen line k -- but at 1920x1080 the overlay's DMA
 *     (all 1920 pixels of a row, one a clock) fetches a row only every
 *     second line, so line L shows row L / 2.  There YSTART goes to y / 2
 *     after each setup and the picture is shown twice as tall (vs);
 *   - the firmware writes each frame into the overlay buffer being shown
 *     (torn lines) and draws its frame rate into it; it cannot pause or seek.
 * Needs the zz9000 driver with ZZ9K_MAP_BOARD (board memory by mmap) and
 * GETREG/SETREG (32-bit registers as two 16-bit halves, high first).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "zzmpeg.h"

extern int munmap();                    /* not in AMIX's sys/mman.h */

/* the AMIX zz9000 driver (zz9000.h) */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };
#define FB_PHYS             0x00200000UL    /* device offset 0 = board + 2 MB */

/* the firmware */
#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define REG_MPEG_FIFOTX     0x2E4
#define REG_OVL_YSTART      0x32C
#define REG_OVL_XSTART      0x330
#define REG_OVL_WIDTH       0x334
#define REG_OVL_HEIGHT      0x338
#define CMD_INIT            1
#define CMD_PROCESS         2
#define CMD_CLOSE           5
#define ST_VIDEO_INIT       0x10
#define ST_FINISHED         0x20
#define SH_STATUS2          4
#define SH_SYNC_TIME        24
#define SH_WIDTH            32
#define SH_HEIGHT           36
#define SH_SIZE             96

/* in board memory, from its start (the ARM's RTG_BASE): clear of soft3D
   (0x0E00000-0x52FFFFF), the overlay (0x6000000, 8 MB at 1920x1080x4) and
   the firmware's audio buffers (0x7000000, 0x7DE0000) */
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00100000UL
#define SHARED_OFF          0x07B00000UL
#define CHUNK               (256 * 1024L)   /* < 512 KB: the firmware's ring is 1 MB */
#define AHEAD               (1536 * 1024L)  /* bytes fed ahead of the picture */

static int fd = -1, f = -1, opened, w, h, oy, verbose, debug, keepy;
/* 2 on a screen too wide for the overlay's DMA (1920 pixels): it fetches a
   buffer row every second screen line, so screen line L shows buffer row
   L / 2 -- the picture is written at row y / 2 and shown twice as tall */
static int vs = 1;
static unsigned char *board, *fifo, *buf;
static int ovl_w, ovl_h;                /* the screen */
static volatile unsigned char *sh;
static long rate, sent;
static unsigned long wr;
static double vt, last_change, last_poll;
static double t_copy, t_regs, t_poll;  /* for ZZM_DEBUG */
static unsigned long st2;               /* status2, as last read */

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

static unsigned long be32(const volatile unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* a double the ARM may be writing while it is read: the same twice */
static double be64d(const volatile unsigned char *p)
{
    union { unsigned char b[8]; double d; } u, v;
    int i, tries = 0;

    do {
        for (i = 0; i < 8; i++)
            u.b[i] = p[i];      /* both sides big-endian IEEE */
        for (i = 0; i < 8; i++)
            v.b[i] = p[i];
    } while ((memcmp(u.b, v.b, 8) || u.d != u.d) && ++tries < 10);
    return u.d == u.d ? u.d : 0.0;
}

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* the stream's rate in bytes a second, from the first MPEG-1 pack header */
static long mux_rate(const unsigned char *p, long n)
{
    long i;

    for (i = 0; i + 12 <= n; i++)
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && p[i + 3] == 0xBA) {
            const unsigned char *q = p + i + 4;

            if ((q[0] & 0xF0) != 0x20)
                return -1;              /* an MPEG-2 pack: not for pl_mpeg */
            return (((long)(q[5] & 0x7F) << 15) | ((long)q[6] << 7) | (q[7] >> 1)) * 50;
        }
    return 0;
}

int zzm_open(const char *file, int x, int y, int verb)
{
    struct zz9k_info info;
    long n;
    int i;

    verbose = verb;
    debug = getenv("ZZM_DEBUG") != NULL;
    keepy = getenv("ZZM_KEEPY") != NULL;    /* test: leave YSTART where the overlay is */
    if (getenv("VPLAY_NOARM"))
        return -1;
    f = open(file, O_RDONLY);
    if (f < 0)
        return -1;
    buf = (unsigned char *)malloc(CHUNK);
    n = buf ? read(f, buf, CHUNK) : -1;
    rate = n > 0 ? mux_rate(buf, n) : 0;
    if (rate <= 0 || lseek(f, 0L, SEEK_SET) < 0)
        goto fail;
    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0)
        goto fail;
    /* info.board_size is the framebuffer's part only; with ZZ9K_MAP_BOARD
       mmap() reaches to the board's end, and refuses what lies beyond */
    board = (unsigned char *)mmap(0, (size_t)(SHARED_OFF + 4096 - FIFO_OFF), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, (off_t)(FIFO_OFF - FB_PHYS));
    if (board == (unsigned char *)-1) {
        board = 0;
        goto fail;
    }
    fifo = board;
    sh = board + (SHARED_OFF - FIFO_OFF);
    for (i = 0; i < SH_SIZE; i++)
        sh[i] = 0;
    ovl_w = info.width;
    ovl_h = info.height;
    /* fixed firmware (C1LD_FEAT_OVL_FETCH, in core 0's loader block, in this mapping): 1:1 */
    vs = ovl_w >= 1600 && !(be32(board + (0x07AFE000UL - FIFO_OFF)) == 0x43314C44UL &&
                            (be32(board + (0x07AFE000UL - FIFO_OFF) + 32) & 1)) ? 2 : 1;
    if (getenv("ZZM_VSCALE"))
        vs = atoi(getenv("ZZM_VSCALE")) == 2 ? 2 : 1;
    oy = y < 0 ? 0 : y;
    reg32(REG_OVL_XSTART, (unsigned long)(x < 0 ? 0 : x));
    reg32(REG_OVL_YSTART, (unsigned long)oy);
    reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
    reg32(REG_MPEG_PARAM0 + 4, info.base);
    reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(REG_MPEG_CMD, CMD_INIT);
    if (!keepy)
        reg32(REG_OVL_YSTART, (unsigned long)(oy / vs));    /* the rows the ARM writes */
    opened = 1;
    sent = 0;
    wr = 0;
    vt = 0;
    st2 = 0;
    last_poll = 0;
    w = h = 0;
    last_change = now();
    if (verbose)
        fprintf(stderr, "vplay: MPEG-1 on the Z3660's ARM, %ld bytes a second\n", rate);
    return 0;
fail:
    if (board)
        munmap((caddr_t)board, (size_t)(SHARED_OFF + 4096 - FIFO_OFF));
    board = 0;
    if (fd >= 0)
        close(fd);
    if (f >= 0)
        close(f);
    fd = f = -1;
    free(buf);
    buf = 0;
    return -1;
}

/*
 * Every 68k access to board memory is served by the ARM's main loop, which
 * stops while the ARM decodes a frame: each read of the shared block can
 * stall the 68060 for a frame's decoding time.  So the block is read at
 * most POLL times a second, and the rest of the time from what was read.
 */
#define POLL                10

static void poll_board(void)
{
    double t;
    static double last_debug;

    if (now() - last_poll < 1.0 / POLL)
        return;
    last_poll = now();
    t = be64d(sh + SH_SYNC_TIME);
    st2 = be32(sh + SH_STATUS2);
    t_poll += now() - last_poll;

    if (debug && now() - last_debug >= 1.0) {
        last_debug = now();
        fprintf(stderr, "zzmpeg: status %08lx time %.2f (kept %.2f) fed %ld KB; last s: copy %.0f regs %.0f poll %.0f ms\n",
                st2, t, vt, sent >> 10, t_copy * 1000, t_regs * 1000, t_poll * 1000);
        t_copy = t_regs = t_poll = 0;
    }

    /* PROCESS rewrites the field with what the 68k side left there, so
       odd values come and go: only a plausible step forward counts */
    if (t > vt && t < vt + 5.0) {
        vt = t;
        last_change = now();
    }
    if (!w && (st2 & ST_VIDEO_INIT) && be32(sh + SH_WIDTH)) {
        w = (int)be32(sh + SH_WIDTH);
        h = (int)be32(sh + SH_HEIGHT);
        if (debug)
            fprintf(stderr, "zzmpeg: %dx%d, overlay at y %d, rows from %d, shown %dx tall\n", w, h, oy,
                    oy / vs, vs);
        reg32(REG_OVL_YSTART, (unsigned long)oy);
        reg32(REG_OVL_WIDTH, (unsigned long)w);
        reg32(REG_OVL_HEIGHT, (unsigned long)(h * vs));     /* re-enables the overlay */
        if (!keepy)
            reg32(REG_OVL_YSTART, (unsigned long)(oy / vs));
        last_change = now();
    }
}

int zzm_pump(void)
{
    long n, first;
    double t0, t1;

    if (!opened)
        return 0;
    poll_board();
    if (f < 0 || sent >= AHEAD + (long)(vt * rate))
        return 0;
    n = read(f, buf, CHUNK);
    if (n <= 0) {
        close(f);
        f = -1;
        last_change = now();
        return 0;
    }
    t0 = now();
    first = (long)(FIFO_SIZE - wr);
    if (n > first) {
        memcpy(fifo + wr, buf, (size_t)first);
        memcpy(fifo, buf + first, (size_t)(n - first));
    } else
        memcpy(fifo + wr, buf, (size_t)n);
    wr = (wr + (unsigned long)n) % FIFO_SIZE;
    sent += n;
    t1 = now();
    reg32(REG_MPEG_FIFOTX, wr);
    reg32(REG_MPEG_CMD, CMD_PROCESS);
    t_copy += t1 - t0;
    t_regs += now() - t1;
    return 1;
}

int zzm_size(int *pw, int *ph)
{
    if (!opened || !w)
        return 0;
    *pw = w;
    *ph = h;
    return 1;
}

/* feed at this many bytes a second instead of the pack header's mux_rate
   (a file muxed without padding has a lower average than its header says) */
void zzm_rate(long bytes)
{
    if (bytes > 0 && (!rate || bytes < rate))
        rate = bytes;
}

/* how many times taller than the picture the overlay shows it */
int zzm_vscale(void)
{
    return vs;
}

double zzm_time(void)
{
    return vt;
}

void zzm_place(int x, int y)
{
    if (!opened)
        return;
    oy = y < 0 ? 0 : y;
    if (debug)
        fprintf(stderr, "zzmpeg: overlay to %d,%d\n", x, oy);
    reg32(REG_OVL_XSTART, (unsigned long)(x < 0 ? 0 : x));
    reg32(REG_OVL_YSTART, (unsigned long)oy);
    if (w) {
        reg32(REG_OVL_HEIGHT, (unsigned long)(h * vs));     /* takes the new place */
        if (!keepy)
            reg32(REG_OVL_YSTART, (unsigned long)(oy / vs));
    }
}

int zzm_done(void)
{
    if (!opened)
        return 1;
    poll_board();
    return (st2 & ST_FINISHED) ||
           (f < 0 && now() - last_change > (vt > 0 ? 2.0 : 20.0));
}

void zzm_close(void)
{
    if (!opened)
        return;
    reg32(REG_MPEG_CMD, CMD_CLOSE);     /* overlay off, decoder freed */
    opened = 0;
    if (f >= 0)
        close(f);
    f = -1;
    munmap((caddr_t)board, (size_t)(SHARED_OFF + 4096 - FIFO_OFF));
    board = 0;
    close(fd);
    fd = -1;
    free(buf);
    buf = 0;
}
