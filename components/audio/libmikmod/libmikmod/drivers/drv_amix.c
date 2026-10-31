/*	MikMod sound library
	(c) 1998, 1999, 2000, 2001, 2002 Miodrag Vallat and others - see file
	AUTHORS for complete list.

	This library is free software; you can redistribute it and/or modify
	it under the terms of the GNU Library General Public License as
	published by the Free Software Foundation; either version 2 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Library General Public License for more details.

	You should have received a copy of the GNU Library General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
	02111-1307, USA.
*/

/*==============================================================================

  Driver for output on AMIX (Commodore Amiga UNIX)'s /dev/audio device.

  /dev/audio on AMIX is a small custom SVR4 STREAMS driver (see
  usr/sys/amiga/driver/audio.c in this tree) talking directly to the Amiga's
  Paula sound chip. Unlike Sun/OSS-style audio devices, it has no
  format-negotiation ioctls at all: each open() gets one signed-8-bit-mono
  Paula channel, and pitch is set by writing Paula's raw DMA period
  register value via AUDIO_PERIOD (sample rate = Paula clock / period).
  There is no installed public header for these ioctls -- every userspace
  consumer in this tree (e.g. usr/amiga/src/cmd/clksnd/au.c) keeps its own
  copy, so this driver does too.

  This driver mixes to 8-bit stereo in software (libmikmod's VC_ mixer)
  at a fixed rate chosen from a fixed period, and outputs real left/right
  audio by opening /dev/audio TWICE -- the kernel driver hands out one of
  Paula's 4 real hardware channels per open() (see grab_channel() in
  usr/sys/amiga/driver/audio.c), and that source documents the physical
  wiring: two channels feed the left speaker, two feed the right, fixed
  in Amiga hardware on every model. There is no ioctl to request a
  specific channel, though -- we just get "next free" -- so this counts
  on opening left before right, with nothing else holding channels 0/1,
  to land correctly. That's a real, documented assumption, not a
  guaranteed contract; a robust fix would need a channel-select ioctl
  added to the kernel driver, which is out of scope here.

==============================================================================*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mikmod_internals.h"

#ifdef DRV_AMIX

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>

/* AMIX /dev/audio ioctls, copied from usr/sys/amiga/driver/audio.h */
#define AMIX_AUIOC        ('U'<<8)
#define AMIX_AUDIO_VOLUME (AMIX_AUIOC|1)
#define AMIX_AUDIO_PERIOD (AMIX_AUIOC|2)
#define AMIX_AUDIO_STEREO (AMIX_AUIOC|3) /* port kernels: one fd, locked L/R pair */

#define AMIX_MAX_VOLUME     63   /* BLOUD in audio.h: hardware volume is 0-63 */
#define AMIX_MIN_PERIOD    124   /* Paula's documented safe minimum period */
#define AMIX_DEFAULT_PERIOD 128  /* ~27.7kHz @ PAL clock: good tracker quality,
                                     small safety margin over AMIX_MIN_PERIOD */

#define AMIX_CLOCK_PAL  3546895UL
#define AMIX_CLOCK_NTSC 3579545UL

#define SOUNDDEVICE "/dev/audio"
/* The kernel driver (usr/sys/amiga/driver/audio.c) only actively fills two
   buffer slots ahead per write -- postaudio()'s switch(pending) handles
   case 0 (fill first slot) and case 1 (fill second slot), then does
   nothing further until an interrupt frees one up -- and each slot is
   capped at AUDIO_BUFFER_SIZE (8192 bytes). A single write bigger than
   that doesn't get fully drained by that logic in one pass: confirmed on
   real hardware that 32768 caused audible stalling/repeating. FRAGSIZE
   here is the size of the INTERLEAVED stereo buffer VC_WriteBytes fills;
   each channel's actual per-write size is FRAGSIZE/2 (4096), safely under
   that 8192 one-slot ceiling -- don't push this past 16384 without
   re-checking that driver logic. */
#define FRAGSIZE 8192

static int sndfd_l = -1;
static int sndfd_r = -1;
static int amix_locked = 0;	/* kernel AUDIO_STEREO: interleaved on sndfd_l */
static int amix_mono = 0;	/* AMIX_AUDIO_MONO=1: one channel, no L/R drift */
static unsigned long amix_clock = AMIX_CLOCK_PAL;
static int amix_period = AMIX_DEFAULT_PERIOD;
static SBYTE *audiobuffer = NULL;	/* interleaved L/R, from VC_WriteBytes */
static SBYTE *leftbuffer = NULL;	/* deinterleaved, FRAGSIZE/2 each */
static SBYTE *rightbuffer = NULL;

/* libmikmod's 8-bit output is unsigned (silence 128), Paula plays signed:
   every byte goes through this table, which also applies AMIX_AUDIO_GAIN
   (e.g. 2 or 1.5; default 1, range 0.1-8), clipping at full scale. */
static SBYTE amix_xlat[256];

static void amix_setup_xlat(void)
{
	double gain = 1.0;
	char *e = getenv("AMIX_AUDIO_GAIN");
	int u;

	if (e && atof(e) > 0)
		gain = atof(e);
	if (gain < 0.1)
		gain = 0.1;
	if (gain > 8.0)
		gain = 8.0;
	for (u = 0; u < 256; u++) {
		double v = (u - 128) * gain;

		if (v > 127)
			v = 127;
		if (v < -128)
			v = -128;
		amix_xlat[u] = (SBYTE)(v < 0 ? v - 0.5 : v + 0.5 > 127 ? 127 : v + 0.5);
	}
}

static void amix_convert(SBYTE *buf, int n)
{
	int i;

	for (i = 0; i < n; i++)
		buf[i] = amix_xlat[(UBYTE)buf[i]];
}

/* Splits VC_WriteBytes' interleaved 8-bit stereo output into the two
   per-channel buffers each get written to their own /dev/audio fd. */
static void amix_deinterleave(SBYTE *stereo, int nbytes)
{
	int i, n = nbytes / 2;

	for (i = 0; i < n; i++) {
		leftbuffer[i] = amix_xlat[(UBYTE)stereo[2*i]];
		rightbuffer[i] = amix_xlat[(UBYTE)stereo[2*i + 1]];
	}
}

static void AMIX_CommandLine(CHAR *cmdline)
{
	CHAR *ptr;

	if ((ptr = MD_GetAtom("ntsc", cmdline, 1)) != NULL) {
		amix_clock = AMIX_CLOCK_NTSC;
		free(ptr);
	}

	if ((ptr = MD_GetAtom("period", cmdline, 0)) != NULL) {
		int p = atoi(ptr);

		if (p >= AMIX_MIN_PERIOD && p <= 65535)
			amix_period = p;
		free(ptr);
	}
}

static BOOL AMIX_IsThere(void)
{
	return access(SOUNDDEVICE, W_OK) == 0;
}

static int amix_setup_channel(int fd)
{
	int volume;

	if (ioctl(fd, AMIX_AUDIO_PERIOD, &amix_period) < 0)
		return 1;

	volume = AMIX_MAX_VOLUME;
	if (ioctl(fd, AMIX_AUDIO_VOLUME, &volume) < 0)
		return 1;

	return 0;
}

static int AMIX_Init(void)
{
	amix_setup_xlat();
	/* Left first, then right -- see the wiring/ordering note at the top
	   of this file. Both need the same period so they stay in pitch. */
	sndfd_l = open(SOUNDDEVICE, O_WRONLY);
	if (sndfd_l < 0) {
		_mm_errno = MMERR_OPENING_AUDIO;
		return 1;
	}

	amix_mono = getenv("AMIX_AUDIO_MONO") != NULL;
	amix_locked = 0;
	if (!amix_mono) {
		/* Kernels with AUDIO_STEREO drive a left and a right channel from
		   one stream, started together and serviced by one interrupt, so
		   they cannot drift apart.  Without it: mono, or the old two opens
		   (left/right drift) with AMIX_AUDIO_DUALOPEN=1. */
		int one = 1;

		if (ioctl(sndfd_l, AMIX_AUDIO_STEREO, &one) == 0)
			amix_locked = 1;
		else if (getenv("AMIX_AUDIO_DUALOPEN") == NULL)
			amix_mono = 1;	/* clean mono beats drifting two-open stereo */
	}
	if (amix_mono || amix_locked) {
		audiobuffer = (SBYTE *)_mm_malloc(FRAGSIZE);
		if (!audiobuffer) {
			close(sndfd_l);
			sndfd_l = -1;
			return 1;
		}
		if (amix_locked)
			md_mode = (md_mode | DMODE_STEREO) & ~DMODE_16BITS;
		else
			md_mode &= ~(DMODE_STEREO | DMODE_16BITS);
		md_mixfreq = (UWORD)(amix_clock / (unsigned long)amix_period);
		if (amix_setup_channel(sndfd_l)) {
			_mm_errno = MMERR_AMIX_INIT;
			return 1;
		}
		{
			int r = VC_Init();
			if (getenv("AMIX_AUDIO_DEBUG"))
				fprintf(stderr, "drv_amix: locked=%d mono=%d md_mode=0x%x mixfreq=%u period=%d gain=%s\n",
					amix_locked, amix_mono, (unsigned)md_mode, (unsigned)md_mixfreq, amix_period,
					getenv("AMIX_AUDIO_GAIN") ? getenv("AMIX_AUDIO_GAIN") : "1");
			return r;
		}
	}

	sndfd_r = open(SOUNDDEVICE, O_WRONLY);
	if (sndfd_r < 0) {
		_mm_errno = MMERR_OPENING_AUDIO;
		close(sndfd_l);
		sndfd_l = -1;
		return 1;
	}

	audiobuffer = (SBYTE *)_mm_malloc(FRAGSIZE);
	leftbuffer = (SBYTE *)_mm_malloc(FRAGSIZE / 2);
	rightbuffer = (SBYTE *)_mm_malloc(FRAGSIZE / 2);
	if (!audiobuffer || !leftbuffer || !rightbuffer)
		goto fail;

	/* /dev/audio only ever does signed 8-bit, but real stereo now
	   (two channels instead of forcing mono) */
	md_mode = (md_mode | DMODE_STEREO) & ~DMODE_16BITS;
	md_mixfreq = (UWORD)(amix_clock / (unsigned long)amix_period);

	if (amix_setup_channel(sndfd_l) || amix_setup_channel(sndfd_r)) {
		_mm_errno = MMERR_AMIX_INIT;
		goto fail;
	}

	return VC_Init();

fail:
	_mm_free(audiobuffer);
	_mm_free(leftbuffer);
	_mm_free(rightbuffer);
	audiobuffer = leftbuffer = rightbuffer = NULL;
	close(sndfd_l);
	close(sndfd_r);
	sndfd_l = sndfd_r = -1;
	return 1;
}

/* usr/sys/amiga/driver/audio.c's audioclose() has its pending-request
   cleanup wrapped in #if 0 -- it does NOT wait for in-flight DMA before
   the client struct is freed. If audio_channel[i].client is still
   pointing at that freed struct when the next DMA-complete interrupt
   fires, audiointr() dereferences freed memory -- confirmed on real
   hardware as a kernel panic when closing during active playback with a
   large per-write size (more in-flight audio at quit time = bigger
   window for this). We can't fix the kernel driver from here, so
   instead: wait out however much audio could still be queued per channel
   (at most two FRAGSIZE/2-sized slots) before closing either fd, giving
   the interrupt handler time to run its normal "no more audio, give up
   this port" cleanup -- which does correctly null out
   audio_channel[i].client -- before we free anything on our side. */
static void amix_drain(void)
{
	struct timeval tv;
	unsigned long usecs, rate;

	rate = md_mixfreq ? md_mixfreq : 8000;
	usecs = (unsigned long)(2 * (FRAGSIZE / 2)) * 1000000UL / rate;
	usecs += usecs / 2; /* 50% safety margin */

	tv.tv_sec = usecs / 1000000UL;
	tv.tv_usec = usecs % 1000000UL;
	select(0, NULL, NULL, NULL, &tv);
}

static void AMIX_Exit(void)
{
	VC_Exit();
	_mm_free(audiobuffer);
	_mm_free(leftbuffer);
	_mm_free(rightbuffer);
	audiobuffer = leftbuffer = rightbuffer = NULL;
	if (sndfd_l >= 0 || sndfd_r >= 0) {
		amix_drain();
		if (sndfd_l >= 0) {
			close(sndfd_l);
			sndfd_l = -1;
		}
		if (sndfd_r >= 0) {
			close(sndfd_r);
			sndfd_r = -1;
		}
	}
}

static void AMIX_Update(void)
{
	int done;

	done = VC_WriteBytes(audiobuffer, FRAGSIZE);
	if (amix_mono || amix_locked) {
		amix_convert(audiobuffer, done);
		if (getenv("AMIX_AUDIO_DEBUG")) {
			static int n;
			if (n++ < 3)
				fprintf(stderr, "drv_amix: update %d bytes -> %d\n", done,
					(int)write(sndfd_l, audiobuffer, done));
			else
				write(sndfd_l, audiobuffer, done);
		} else
			write(sndfd_l, audiobuffer, done);
		return;
	}
	amix_deinterleave(audiobuffer, done);
	write(sndfd_l, leftbuffer, done / 2);
	write(sndfd_r, rightbuffer, done / 2);
}

static void AMIX_Pause(void)
{
	int done;

	done = VC_SilenceBytes(audiobuffer, FRAGSIZE);
	if (amix_mono || amix_locked) {
		amix_convert(audiobuffer, done);
		write(sndfd_l, audiobuffer, done);
		return;
	}
	amix_deinterleave(audiobuffer, done);
	write(sndfd_l, leftbuffer, done / 2);
	write(sndfd_r, rightbuffer, done / 2);
}

MIKMODAPI MDRIVER drv_amix = {
	NULL,
	"AMIX Audio",
	"AMIX (Commodore Amiga UNIX) /dev/audio driver v1.0",
	0, 255,
	"amix",

	AMIX_CommandLine,
	AMIX_IsThere,
	VC_SampleLoad,
	VC_SampleUnload,
	VC_SampleSpace,
	VC_SampleLength,
	AMIX_Init,
	AMIX_Exit,
	NULL,
	VC_SetNumVoices,
	VC_PlayStart,
	VC_PlayStop,
	AMIX_Update,
	AMIX_Pause,
	VC_VoiceSetVolume,
	VC_VoiceGetVolume,
	VC_VoiceSetFrequency,
	VC_VoiceGetFrequency,
	VC_VoiceSetPanning,
	VC_VoiceGetPanning,
	VC_VoicePlay,
	VC_VoiceStop,
	VC_VoiceStopped,
	VC_VoiceGetPosition,
	VC_VoiceRealVolume
};

#else

MISSING(drv_amix);

#endif

/* ex:set ts=4: */
