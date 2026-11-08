/*
 * snd_c1.c -- ioquake3's sound on the Z3660's core 1, in place of
 * sdl/sdl_snd.c: the engine mixes 16-bit stereo at 48 kHz (the HDMI ring's
 * rate, no resampling after the mixer) into its own ring here, 64K frames
 * (1.4 s, a power of 2 as snd_mix.c wants); the platform's timer interrupt
 * copies what is painted on into the board's HDMI ring ahead of the DMA
 * (core1_main.c), so a frame slower than that ring (160 ms) only makes a
 * gap of silence.  s_mixahead must cover a frame: c1q3 sets it.
 */
#include "client/snd_local.h"
#include "c1q3.h"

#define RING_FRAMES 65536

qboolean SNDDMA_Init(void)
{
    if (!dma.buffer)
        dma.buffer = calloc(RING_FRAMES, 4);
    if (!dma.buffer) {
        Com_Printf("SNDDMA_Init: no memory for the ring\n");
        return qfalse;
    }
    dma.samplebits = 16;
    dma.isfloat = qfalse;
    dma.channels = 2;
    dma.fullsamples = RING_FRAMES;
    dma.samples = RING_FRAMES * 2;
    dma.submission_chunk = 1;
    dma.speed = C1P_RATE;
    c1p_snd_start((int16_t *)dma.buffer, RING_FRAMES, &s_paintedtime);
    Com_Printf("SNDDMA_Init: 48 kHz 16-bit stereo to the board's HDMI\n");
    return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
    return (int)((c1p_snd_now() & (RING_FRAMES - 1)) * 2);
}

void SNDDMA_Shutdown(void)
{
    c1p_snd_stop();
    free(dma.buffer);
    dma.buffer = NULL;
}

void SNDDMA_BeginPainting(void)
{
}

void SNDDMA_Submit(void)
{
}
