/*
 * amix_snd.c -- ioquake3's sound DMA on AMIX, in place of sdl/sdl_snd.c,
 * through Paula itself (/dev/paula, the AMIX 040/060 port's native Paula
 * driver), as the Amiga ports do it with audio.device (2026-10-27).
 *
 * The engine mixes as everywhere (Paula has four voices, a game has dozens
 * of sounds), into a 16-bit stereo ring here.  Paula plays a looping ring
 * of the same length in chip RAM: channel 0 the left half, channel 1 the
 * right half, 8-bit signed, at the period nearest s_khz (22 by default:
 * 3546895 / 161 = 22030 Hz).  After each mix SNDDMA_Submit converts what
 * was painted since the last one and copies it to the chip ring
 * (PAULA_LOAD); Paula's play position is taken from the clock since the
 * DMA started, Paula having no readable one.  The ring holds about 1.5 s,
 * so the engine can mix ahead over the long frames of a 68060 (q3.sh sets
 * s_mixahead).
 *
 * Without /dev/paula (or with it busy: pt2, mikmod) the game runs silent.
 *
 * With the GL on the Z3660's core 1 (AMIX_ZZGL, 2026-10-30) the sound goes
 * to the board's HDMI instead (s_hdmi 1, the default there): the engine
 * mixes at 24 kHz (48 with s_khz 44/48) into its ring here, Submit copies
 * what it painted into zzgl's stream in board memory at its time's offset,
 * and core 1 plays it, doubling to 48 kHz; the position comes from core 1.
 * Paula stays free.  Without core 1's server: Paula as before.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "../code/qcommon/q_shared.h"
#include "../code/client/snd_local.h"
#include "paula.h"

#define FRAMES     32768                /* ring length, a power of 2 (mask) */
#define PAULA_CLK  3546895.0            /* PAL */

static int pfd = -1;
static short *ring;                     /* the engine's: FRAMES stereo frames */
static signed char *chipL, *chipR;      /* what the chip ring holds */
static double t0, rate;
static int submitted = -1;              /* frames copied up to (s_paintedtime) */

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

static void nap(int ms)
{
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = ms * 1000;
    select(0, NULL, NULL, NULL, &tv);
}

static void op(struct paula_tick *t, int o, int ch, int val, unsigned long off)
{
    struct paula_op *p = &t->op[t->nops++];
    p->op = (unsigned char) o;
    p->ch = (unsigned char) ch;
    p->val = (unsigned short) val;
    p->off = off;
}

#ifdef AMIX_ZZGL
#include "zzgl_platform.h"

static int hdmi;                        /* the HDMI path is on */

static qboolean hdmi_init(void)
{
    cvar_t *khz = Cvar_Get("s_khz", "22", CVAR_ARCHIVE);
    const int div = khz->integer >= 44 ? 1 : 2;

    if (zzgl_sound_open(div) < 0)
        return qfalse;
    ring = (short *) calloc(FRAMES * 2, sizeof (short));
    if (!ring) {
        zzgl_sound_close();
        return qfalse;
    }
    dma.channels = 2;
    dma.samples = FRAMES * 2;
    dma.fullsamples = FRAMES;
    dma.submission_chunk = 1;
    dma.samplebits = 16;
    dma.isfloat = 0;
    dma.speed = 48000 / div;
    dma.buffer = (byte *) ring;
    submitted = -1;
    hdmi = 1;
    Com_Printf("SNDDMA_Init: the board's HDMI through the Z3660's core 1, %d Hz, 16-bit stereo\n", dma.speed);
    return qtrue;
}

static void hdmi_submit(void)
{
    int end = s_paintedtime, start = submitted;

    if (start < 0 || end - start > FRAMES || end < start)
        start = end - FRAMES;
    if (start < 0)
        start = 0;
    while (start < end) {
        /* in pieces up to the end of the ring */
        const int i = start & (FRAMES - 1);
        const int n = end - start < FRAMES - i ? end - start : FRAMES - i;

        zzgl_sound_write((unsigned long) start * 4, ring + i * 2, (unsigned long) n * 4);
        start += n;
    }
    submitted = end;
}
#endif

qboolean SNDDMA_Init(void)
{
    cvar_t *khz = Cvar_Get("s_khz", "22", CVAR_ARCHIVE);
    struct paula_tick t;
    struct paula_status st;
    unsigned long played;
    int period, i;

#ifdef AMIX_ZZGL
    if (Cvar_Get("s_hdmi", "1", CVAR_ARCHIVE)->integer && hdmi_init())
        return qtrue;
#endif
    period = (int) (PAULA_CLK / (khz->integer >= 44 ? 28000 : khz->integer <= 11 ? 11025 : 22050) + 0.5);
    if (period < 124)
        period = 124;                   /* Paula's limit with DMA */

    pfd = open("/dev/paula", O_RDWR);
    if (pfd < 0) {
        Com_Printf("SNDDMA_Init: /dev/paula: %s -- no sound\n", strerror(errno));
        return qfalse;
    }
    if (ioctl(pfd, PAULA_SETUP, (char *) (2L * FRAMES)) < 0) {
        Com_Printf("SNDDMA_Init: no %d bytes of chip RAM: %s -- no sound\n", 2 * FRAMES, strerror(errno));
        close(pfd);
        pfd = -1;
        return qfalse;
    }
    ring = (short *) calloc(FRAMES * 2, sizeof (short));
    chipL = (signed char *) calloc(FRAMES, 1);
    chipR = (signed char *) calloc(FRAMES, 1);
    if (!ring || !chipL || !chipR) {
        SNDDMA_Shutdown();
        return qfalse;
    }

    rate = PAULA_CLK / period;
    dma.channels = 2;
    dma.samples = FRAMES * 2;
    dma.fullsamples = FRAMES;
    dma.submission_chunk = 1;
    dma.samplebits = 16;
    dma.isfloat = 0;
    dma.speed = (int) (rate + 0.5);
    dma.buffer = (byte *) ring;

    /* both channels loop over their half of the (silent) chip ring */
    t.nops = 0;
    op(&t, PO_DMA, 0, 0x0003, 0);
    for (i = 0; i < 2; i++) {
        op(&t, PO_LEN, i, FRAMES / 2, 0);
        op(&t, PO_LC, i, 0, (unsigned long) i * FRAMES);
        op(&t, PO_PER, i, period, 0);
        op(&t, PO_VOL, i, 64, 0);
    }
    op(&t, PO_DMA, 0, 0x8003, 0);
    for (i = 0; i < 2; i++) {
        op(&t, PO_LC, i, 0, (unsigned long) i * FRAMES);
        op(&t, PO_LEN, i, FRAMES / 2, 0);
    }
    ioctl(pfd, PAULA_STATUS, (char *) &st);
    played = st.played;
    if (ioctl(pfd, PAULA_PUSH, (char *) &t) < 0) {
        Com_Printf("SNDDMA_Init: PAULA_PUSH: %s -- no sound\n", strerror(errno));
        SNDDMA_Shutdown();
        return qfalse;
    }
    /* the DMA starts with the next CIA tick (<= 20 ms) */
    for (i = 0; i < 100; i++) {
        ioctl(pfd, PAULA_STATUS, (char *) &st);
        if (st.played != played)
            break;
        nap(2);
    }
    t0 = now();
    submitted = -1;
    Com_Printf("SNDDMA_Init: Paula channels 0+1, %.0f Hz, 8-bit stereo (/dev/paula)\n", rate);
    return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
    long frames;

#ifdef AMIX_ZZGL
    if (hdmi)
        return (int) (((zzgl_sound_pos() / 4) & (FRAMES - 1)) * 2);
#endif
    if (pfd < 0)
        return 0;
    frames = (long) ((now() - t0) * rate);
    return (int) ((frames & (FRAMES - 1)) * 2);
}

void SNDDMA_Shutdown(void)
{
#ifdef AMIX_ZZGL
    if (hdmi)
        zzgl_sound_close();
    hdmi = 0;
#endif
    if (pfd >= 0)
        close(pfd);             /* the driver stops the channels */
    pfd = -1;
    free(ring);
    free(chipL);
    free(chipR);
    ring = NULL;
    chipL = chipR = NULL;
    dma.buffer = NULL;
}

void SNDDMA_BeginPainting(void)
{
}

/* frames [a, b) of the ring (indices, no wrap) to the chip */
static void load(int a, int b)
{
    struct paula_load ld;

    if (b <= a)
        return;
    ld.off = (unsigned long) a;
    ld.len = (unsigned long) (b - a);
    ld.data = (char *) chipL + a;
    ioctl(pfd, PAULA_LOAD, (char *) &ld);
    ld.off = (unsigned long) (FRAMES + a);
    ld.data = (char *) chipR + a;
    ioctl(pfd, PAULA_LOAD, (char *) &ld);
}

void SNDDMA_Submit(void)
{
    int end, start, f, i, a;

#ifdef AMIX_ZZGL
    if (hdmi) {
        hdmi_submit();
        return;
    }
#endif
    if (pfd < 0)
        return;
    end = s_paintedtime;
    start = submitted;
    if (start < 0 || end - start > FRAMES || end < start)
        start = end - FRAMES;
    if (end <= start)
        return;
    for (f = start; f < end; f++) {
        i = f & (FRAMES - 1);
        chipL[i] = (signed char) (ring[i * 2] >> 8);
        chipR[i] = (signed char) (ring[i * 2 + 1] >> 8);
    }
    a = start & (FRAMES - 1);
    if (a + (end - start) <= FRAMES)
        load(a, a + (end - start));
    else {
        load(a, FRAMES);
        load(0, (end - start) - (FRAMES - a));
    }
    submitted = end;
}
