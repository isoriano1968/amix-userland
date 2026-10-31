/*	MikMod sound library
	(c) 1998-2004 Miodrag Vallat and others - see file AUTHORS for
	complete list.

	This library is free software; you can redistribute it and/or modify
	it under the terms of the GNU Library General Public License as
	published by the Free Software Foundation; either version 2 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Library General Public License for more details.
*/

/*==============================================================================

  Driver for Paula itself on AMIX (Commodore Amiga UNIX): /dev/paula, the
  AMIX 040/060 port's native Paula driver (major 53).

  A hardware-voice driver, like the Gravis Ultrasound one: four voices are
  Paula's four channels, the samples are copied to chip RAM once, and each
  player tick becomes one batch of Paula register writes that the kernel
  applies from CIA-A timer B at the module's tempo.  Nothing is mixed: the
  CPU only runs the player.  Voice n is Paula channel n, so a MOD's
  L R R L channels land where an Amiga puts them.  Panning, 16-bit depth
  and ping-pong loops do not exist on Paula: samples become signed 8-bit,
  ping-pong loops loop forward.

  At most four voices: a module with more channels needs a mixing driver
  (drv_amix; the mikmod player switches by itself, see its AMIX notes).

==============================================================================*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mikmod_internals.h"

#ifdef DRV_PAULA

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include "amix_paula.h"

#define PAULA_VOICES	4
#define MAX_HANDLES	256
#define LEAD_TICKS	6	/* ticks queued ahead of what plays: ~120 ms */
#define PAL_CLOCK	3546895L

typedef struct {
	SBYTE *data;		/* signed 8-bit copy */
	ULONG length;		/* bytes */
	ULONG chipoff;		/* in the driver's area */
} PSAMPLE;

typedef struct {
	SWORD handle;
	ULONG frq, start, size, reppos, repend;
	UWORD vol, flags;
	int kick, stop, changed, active;
	long ticksleft;		/* until a one-shot sample has played */
} PVOICE;

static int pfd = -1;
static PSAMPLE samples[MAX_HANDLES];
static PVOICE voices[PAULA_VOICES];
static int dirty;		/* samples to (re)copy to chip RAM */
static ULONG area;
static int lastbpm;
static struct paula_tick tick;

/* 10 ms: select() sleeps on AMIX; poll() with no descriptors returns at
   once there, and the wait for room in the queue became a busy loop */
static void nap(void)
{
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 10000;
	select(0, NULL, NULL, NULL, &tv);
}

static void op(int o, int ch, UWORD val, ULONG off)
{
	struct paula_op *p;

	if (tick.nops >= PAULA_MAXOPS)
		return;
	p = &tick.op[tick.nops++];
	p->op = (unsigned char)o;
	p->ch = (unsigned char)ch;
	p->val = val;
	p->off = off;
}

static BOOL Paula_IsThere(void)
{
	int fd = open("/dev/paula", O_RDWR);

	if (fd < 0) {
		/* MIKMOD_AMIX_DEBUG=file: why autodetection passed Paula by */
		const char *f = getenv("MIKMOD_AMIX_DEBUG");
		FILE *fp;
		int e = errno;

		if (f && *f && (fp = fopen(f, "a")) != NULL) {
			fprintf(fp, "drv_paula: /dev/paula: %s\n", strerror(e));
			fclose(fp);
		}
		return 0;
	}
	close(fd);
	return 1;
}

static int Paula_Init(void)
{
	pfd = open("/dev/paula", O_RDWR);
	if (pfd < 0) {
		_mm_errno = MMERR_OPENING_AUDIO;
		return 1;
	}
	memset(voices, 0, sizeof(voices));
	area = 0;
	lastbpm = 0;
	dirty = 1;
	/* the music on hardware voices: a program asking for the software
	   mixer (the mikmod player does) would otherwise get none at all */
	md_mode &= ~(DMODE_SOFT_MUSIC | DMODE_SOFT_SNDFX);
	return 0;
}

static void Paula_Exit(void)
{
	int t;

	if (pfd >= 0)
		close(pfd);	/* the driver stops the channels */
	pfd = -1;
	for (t = 0; t < MAX_HANDLES; t++)
		if (samples[t].data) {
			free(samples[t].data);
			samples[t].data = NULL;
		}
	area = 0;
}

static int Paula_Reset(void)
{
	if (pfd >= 0)
		ioctl(pfd, PAULA_RESET, (char *)0);
	return 0;
}

static int Paula_SetNumVoices(void)
{
	return 0;	/* md_hardchn <= 4: HardVoiceLimit */
}

static SWORD Paula_SampleLoad(struct SAMPLOAD *sload, int type)
{
	SAMPLE *s = sload->sample;
	int handle;

	if (type == MD_SOFTWARE)
		return -1;
	for (handle = 0; handle < MAX_HANDLES; handle++)
		if (!samples[handle].data)
			break;
	if (handle == MAX_HANDLES) {
		_mm_errno = MMERR_OUT_OF_HANDLES;
		return -1;
	}
	SL_SampleSigned(sload);
	SL_Sample16to8(sload);
	if (!(samples[handle].data = (SBYTE *)_mm_malloc(s->length + 2))) {
		_mm_errno = MMERR_SAMPLE_TOO_BIG;
		return -1;
	}
	if (SL_Load(samples[handle].data, sload, s->length)) {
		free(samples[handle].data);
		samples[handle].data = NULL;
		return -1;
	}
	samples[handle].length = s->length;
	dirty = 1;
	return (SWORD)handle;
}

static void Paula_SampleUnload(SWORD handle)
{
	if (handle < 0 || handle >= MAX_HANDLES || !samples[handle].data)
		return;
	free(samples[handle].data);
	samples[handle].data = NULL;
	dirty = 1;
}

static ULONG Paula_FreeSampleSpace(int type)
{
	return type == MD_HARDWARE ? (ULONG)PAULA_MAXAREA : 0;
}

static ULONG Paula_RealSampleLength(int type, SAMPLE *s)
{
	return s ? s->length : 0;
}

/* the samples to chip RAM, packed (word-aligned) */
static void upload(void)
{
	ULONG total = 0;
	struct paula_load ld;
	int t;

	for (t = 0; t < MAX_HANDLES; t++)
		if (samples[t].data) {
			samples[t].chipoff = total;
			total += (samples[t].length + 1) & ~1UL;
		}
	if (total < 2)
		total = 2;
	if (total > (ULONG)PAULA_MAXAREA)
		total = PAULA_MAXAREA;
	if (total > area) {
		if (ioctl(pfd, PAULA_SETUP, (char *)total) < 0)
			return;
		area = total;
	}
	for (t = 0; t < MAX_HANDLES; t++)
		if (samples[t].data && samples[t].chipoff + samples[t].length <= area) {
			ld.off = samples[t].chipoff;
			ld.len = samples[t].length;
			ld.data = (char *)samples[t].data;
			ioctl(pfd, PAULA_LOAD, (char *)&ld);
		}
	dirty = 0;
}

static int Paula_PlayStart(void)
{
	memset(voices, 0, sizeof(voices));
	ioctl(pfd, PAULA_RESET, (char *)0);
	lastbpm = 0;
	if (dirty)
		upload();
	return 0;
}

static void Paula_PlayStop(void)
{
	if (pfd >= 0)
		ioctl(pfd, PAULA_RESET, (char *)0);
}

static UWORD period_of(ULONG frq)
{
	ULONG p;

	if (frq < 55)
		return 65535;
	p = (ULONG)(PAL_CLOCK / (long)frq);
	return (UWORD)(p < 113 ? 113 : p > 65535 ? 65535 : p);
}

static UWORD volume_of(UWORD vol)	/* 0..256 -> 0..64 */
{
	UWORD v = (UWORD)((vol * 64 + 128) / 256);

	return v > 64 ? 64 : v;
}

static ULONG chip_of(PVOICE *v, ULONG pos)
{
	PSAMPLE *s;

	if (v->handle < 0 || v->handle >= MAX_HANDLES || !samples[v->handle].data)
		return PAULA_NULLOFF;
	s = &samples[v->handle];
	if (pos >= s->length || s->chipoff + s->length > area)
		return PAULA_NULLOFF;
	return s->chipoff + (pos & ~1UL);
}

static UWORD words_of(ULONG bytes)
{
	ULONG w = bytes / 2;

	return (UWORD)(w == 0 ? 1 : w > 65535 ? 65535 : w);
}

/* one player tick -> one batch, in ProTracker's order */
static void one_tick(void)
{
	int t;
	UWORD off = 0, on = 0;
	long tickus;

	tick.nops = 0;
	md_player();

	if (md_bpm != lastbpm) {
		lastbpm = md_bpm;
		op(PO_TEMPO, 0, (UWORD)(lastbpm < 32 ? 32 : lastbpm > 255 ? 255 : lastbpm), 0);
	}
	tickus = 2500000L / (lastbpm ? lastbpm : 125);

	for (t = 0; t < PAULA_VOICES; t++)
		if (voices[t].kick || voices[t].stop)
			off |= 1 << t;
	if (off)
		op(PO_DMA, 0, off, 0);

	for (t = 0; t < PAULA_VOICES; t++) {
		PVOICE *v = &voices[t];

		if (v->kick) {
			op(PO_LEN, t, words_of(v->size - v->start), 0);
			op(PO_LC, t, 0, chip_of(v, v->start));
			on |= 1 << t;
		}
		if (v->kick || v->changed) {
			op(PO_PER, t, period_of(v->frq), 0);
			op(PO_VOL, t, v->stop ? 0 : volume_of(v->vol), 0);
		}
	}
	if (on)
		op(PO_DMA, 0, 0x8000 | on, 0);

	for (t = 0; t < PAULA_VOICES; t++) {
		PVOICE *v = &voices[t];

		if (v->kick) {
			/* what Paula plays after the first pass */
			if ((v->flags & SF_LOOP) && v->repend > v->reppos) {
				op(PO_LC, t, 0, chip_of(v, v->reppos));
				op(PO_LEN, t, words_of(v->repend - v->reppos), 0);
				v->ticksleft = -1;
			} else {
				op(PO_LC, t, 0, PAULA_NULLOFF);
				op(PO_LEN, t, 1, 0);
				v->ticksleft = v->frq ? (long)((v->size - v->start) * 1000000.0 / v->frq / tickus) + 1 : 0;
			}
		} else if (v->active && v->ticksleft > 0 && --v->ticksleft == 0)
			v->active = 0;
		v->kick = v->stop = v->changed = 0;
	}

	while (ioctl(pfd, PAULA_PUSH, (char *)&tick) < 0 && errno == EAGAIN)
		nap();
}

/* produce ticks until LEAD_TICKS are queued; wait when far enough ahead */
static void Paula_Update(void)
{
	struct paula_status st;

	if (pfd < 0)
		return;
	if (dirty)
		upload();
	for (;;) {
		if (ioctl(pfd, PAULA_STATUS, (char *)&st) < 0)
			return;
		if (st.queued < LEAD_TICKS)
			break;
		nap();
	}
	while (st.queued++ < LEAD_TICKS)
		one_tick();
}

static void Paula_VoiceSetVolume(UBYTE voice, UWORD vol)
{
	if (voice < PAULA_VOICES && voices[voice].vol != vol) {
		voices[voice].vol = vol;
		voices[voice].changed = 1;
	}
}

static UWORD Paula_VoiceGetVolume(UBYTE voice)
{
	return voice < PAULA_VOICES ? voices[voice].vol : 0;
}

static void Paula_VoiceSetFrequency(UBYTE voice, ULONG frq)
{
	if (voice < PAULA_VOICES && voices[voice].frq != frq) {
		voices[voice].frq = frq;
		voices[voice].changed = 1;
	}
}

static ULONG Paula_VoiceGetFrequency(UBYTE voice)
{
	return voice < PAULA_VOICES ? voices[voice].frq : 0;
}

static void Paula_VoiceSetPanning(UBYTE voice, ULONG pan)
{
	/* Paula's channels sit where they sit: 0 and 3 left, 1 and 2 right */
}

static ULONG Paula_VoiceGetPanning(UBYTE voice)
{
	return (voice == 0 || voice == 3) ? PAN_LEFT : PAN_RIGHT;
}

static void Paula_VoicePlay(UBYTE voice, SWORD handle, ULONG start, ULONG size,
							ULONG reppos, ULONG repend, UWORD flags)
{
	PVOICE *v;

	if (voice >= PAULA_VOICES || start >= size)
		return;
	v = &voices[voice];
	if ((flags & SF_LOOP) && repend > size)
		repend = size;
	v->handle = handle;
	v->start = start;
	v->size = size;
	v->reppos = reppos;
	v->repend = repend;
	v->flags = flags;
	v->kick = 1;
	v->stop = 0;
	v->active = 1;
}

static void Paula_VoiceStop(UBYTE voice)
{
	if (voice < PAULA_VOICES && voices[voice].active) {
		voices[voice].active = 0;
		voices[voice].kick = 0;
		voices[voice].stop = 1;
	}
}

static BOOL Paula_VoiceStopped(UBYTE voice)
{
	return voice < PAULA_VOICES ? !voices[voice].active : 1;
}

static SLONG Paula_VoiceGetPosition(UBYTE voice)
{
	return -1;	/* Paula does not tell */
}

static ULONG Paula_VoiceRealVolume(UBYTE voice)
{
	return voice < PAULA_VOICES && voices[voice].active ? voices[voice].vol * 128 : 0;
}

MIKMODAPI MDRIVER drv_paula = {
	NULL,
	"Paula (AMIX /dev/paula)",
	"AMIX native Paula driver v1.0, no mixing",
	PAULA_VOICES, 0,
	"paula",
	NULL,
	Paula_IsThere,
	Paula_SampleLoad,
	Paula_SampleUnload,
	Paula_FreeSampleSpace,
	Paula_RealSampleLength,
	Paula_Init,
	Paula_Exit,
	Paula_Reset,
	Paula_SetNumVoices,
	Paula_PlayStart,
	Paula_PlayStop,
	Paula_Update,
	NULL,
	Paula_VoiceSetVolume,
	Paula_VoiceGetVolume,
	Paula_VoiceSetFrequency,
	Paula_VoiceGetFrequency,
	Paula_VoiceSetPanning,
	Paula_VoiceGetPanning,
	Paula_VoicePlay,
	Paula_VoiceStop,
	Paula_VoiceStopped,
	Paula_VoiceGetPosition,
	Paula_VoiceRealVolume
};

#else

MISSING(drv_paula);

#endif

/* ex:set ts=4: */
