/*
 * snd_zzgl.c -- Quake's sound through the board's HDMI on AMIX: the 060 mixes
 * into a ring here as into a DMA buffer, what is new goes into zzgl's stream
 * in board memory, and core 1 plays that.  The window code has the server up
 * by the time S_Init gets here.
 */
#include <stdlib.h>
#include "quakedef.h"
#include "zzgl.h"
#include "zzgl_platform.h"

#define DIV     2                       /* 48000 / 2 */
#define FRAMES  ZZGL_SND_FRAMES

static short *ring;
static int on, submitted;
extern int paintedtime;

qboolean SNDDMA_Init(void)
{
	if (zzgl_sound_open(DIV) < 0) {
		Con_Printf("No sound: the board's HDMI is not there\n");
		return 0;
	}
	ring = calloc(FRAMES * 2, sizeof(short));
	if (!ring) {
		zzgl_sound_close();
		return 0;
	}
	shm = &sn;
	shm->splitbuffer = 0;
	shm->samplebits = 16;
	shm->speed = ZZGL_HDMI_RATE / DIV;
	shm->channels = 2;
	shm->samples = FRAMES * 2;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *)ring;
	submitted = -1;
	on = 1;
	Con_Printf("Sound on the board's HDMI, %d Hz, 16-bit stereo\n", shm->speed);
	return 1;
}

int SNDDMA_GetDMAPos(void)
{
	if (!on)
		return 0;
	shm->samplepos = (int)((zzgl_sound_pos() / 4) & (FRAMES - 1)) * 2;
	return shm->samplepos;
}

void SNDDMA_Submit(void)
{
	int end = paintedtime, start = submitted;

	if (!on)
		return;
	if (start < 0 || end - start > FRAMES || end < start)
		start = end - FRAMES;
	if (start < 0)
		start = 0;
	while (start < end) {
		/* in pieces up to the end of the ring */
		const int i = start & (FRAMES - 1);
		const int n = end - start < FRAMES - i ? end - start : FRAMES - i;

		zzgl_sound_write((unsigned long)start * 4, ring + i * 2, (unsigned long)n * 4);
		start += n;
	}
	submitted = end;
}

void SNDDMA_Shutdown(void)
{
	if (on)
		zzgl_sound_close();
	on = 0;
}
