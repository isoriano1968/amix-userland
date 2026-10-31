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

	You should have received a copy of the GNU Library General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
	02111-1307, USA.
*/

/*==============================================================================

  Driver for AMIX: sound on the Z3660's HDMI output.

  The 060 mixes (libmikmod's VC_ mixer) at 24 kHz, 16-bit stereo, into the
  stream in board memory; the gpu program on the board's core 1 feeds that to
  the HDMI ring and reports what has been played.  The numbers are amix-gate's:
  firmware/core1 zzgl.h and cores/h264bench/bench.h.  Build with those on the
  include path and -DDRV_HDMI (components/audio/libmikmod's Makefile does).

  Present when core 1 runs z3660ARM_Core_gpu.  No GL and no window: without a
  server the driver starts one and ends it again, with a program's server (GL
  on core 1) it plays alongside.

==============================================================================*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef DRV_HDMI

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "mikmod_internals.h"
#include "bench.h"
#include "zzgl.h"

extern int munmap();

#define ZZ9KIOC_SETREG		(('Z' << 8) | 7)
struct zz9k_reg { unsigned short offset, value; };

#define FB_PHYS			0x00200000UL
#define PAGE			4096UL
#define REG_AUDIO_PARAM		0x1EC
#define REG_AUDIO_VAL		0x1F0

#define RATE			24000
#define DIV			(ZZGL_HDMI_RATE / RATE)
#define LEAD			(RATE * 4 / 2)	/* bytes ahead of what is heard */
#define CHUNK			2048

static int fd = -1, started;
static volatile unsigned long *ctl, *blk;
static unsigned char *stream;
static unsigned long mixed;
static short *mix;

static void *board(unsigned long off, unsigned long len)
{
	void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

	return p == (void *)-1 ? NULL : p;
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

static int runs_gpu(void)
{
	return ctl[W_MAGIC] == HB_MAGIC_READY && ctl[W_NAME] == ('g' << 24 | 'p' << 16 | 'u' << 8);
}

/* core 1's server: running already, or started here */
static int server(void)
{
	volatile unsigned long *data;
	int i;

	if (blk[ZB_MAGIC] == ZZGL_MAGIC && ctl[W_STATE] == HB_RUNNING)
		return 0;
	if (ctl[W_STATE] == HB_POSTED || ctl[W_STATE] == HB_RUNNING)
		return -1;
	if (!(data = board(HB_DATA_OFF, PAGE)))
		return -1;
	blk[ZB_MAGIC] = 0;
	data[0] = ZZGL_JOB_MAGIC;
	ctl[W_DATA_OFF] = HB_DATA_OFF;
	ctl[W_DATA_SIZE] = 4;
	ctl[W_FLAGS] = 0;
	ctl[W_SEQ] = ctl[W_SEQ] + 1;
	ctl[W_STATE] = HB_POSTED;
	munmap((caddr_t)data, (size_t)PAGE);
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
	munmap((caddr_t)ring, (size_t)ZZGL_RING_SIZE);
	started = 0;
}

static BOOL HDMI_IsPresent(void)
{
	BOOL present = 0;

	if ((fd = open("/dev/zz9000", O_RDWR)) >= 0) {
		if ((ctl = board(HB_CTL_OFF, PAGE)) != NULL) {
			present = runs_gpu();
			munmap((caddr_t)ctl, (size_t)PAGE);
		}
		close(fd);
	}
	fd = -1;
	ctl = NULL;
	return present;
}

static int HDMI_Init(void)
{
	if (!HDMI_IsPresent() || (fd = open("/dev/zz9000", O_RDWR)) < 0) {
		_mm_errno = MMERR_OPENING_AUDIO;
		return 1;
	}
	ctl = board(HB_CTL_OFF, PAGE);
	blk = board(ZZGL_BLK_OFF, PAGE);
	stream = board(ZZGL_SND_OFF, ZZGL_SND_SIZE);
	mix = (short *)malloc(CHUNK);
	if (!ctl || !blk || !stream || !mix || server() < 0) {
		_mm_errno = MMERR_OPENING_AUDIO;
		return 1;
	}
	memset(stream, 0, ZZGL_SND_SIZE);
	blk[ZB_SND_ON] = 0;
	blk[ZB_SND_WR] = 0;
	reg32(REG_AUDIO_PARAM, 0);
	reg32(REG_AUDIO_VAL, ZZGL_HDMI_OFF);
	usleep(100000);				/* core 0's main loop takes it */
	blk[ZB_SND_POS] = 0;
	blk[ZB_SND_ON] = DIV;
	mixed = 0;

	md_mixfreq = RATE;
	md_mode |= DMODE_16BITS | DMODE_STEREO | DMODE_SOFT_MUSIC | DMODE_SOFT_SNDFX;
	return VC_Init();
}

static void HDMI_Exit(void)
{
	VC_Exit();
	if (blk) {
		unsigned char *ring;

		blk[ZB_SND_ON] = 0;
		usleep(20000);			/* core 1 lets go of the ring */
		if ((ring = board(ZZGL_HDMI_OFF, ZZGL_HDMI_SIZE)) != NULL) {
			/* the DMA loops it for ever: silence, not the last notes */
			memset(ring, 0, ZZGL_HDMI_SIZE);
			munmap((caddr_t)ring, (size_t)ZZGL_HDMI_SIZE);
		}
		server_end();
	}
	if (fd >= 0)
		close(fd);
	fd = -1;
	if (mix)
		free(mix);
	mix = NULL;
	ctl = blk = NULL;
	stream = NULL;
}

static void HDMI_Update(void)
{
	const unsigned long want = blk[ZB_SND_POS] / DIV + LEAD;

	while (mixed < want) {
		const unsigned long off = mixed % ZZGL_SND_SIZE;

		VC_WriteBytes((SBYTE *)mix, CHUNK);
		if (ZZGL_SND_SIZE - off >= CHUNK) {
			memcpy(stream + off, mix, CHUNK);
		} else {
			memcpy(stream + off, mix, ZZGL_SND_SIZE - off);
			memcpy(stream, (char *)mix + (ZZGL_SND_SIZE - off), CHUNK - (ZZGL_SND_SIZE - off));
		}
		mixed += CHUNK;
		blk[ZB_SND_WR] = mixed;
	}
}

MIKMODAPI MDRIVER drv_hdmi={
	NULL,
	"HDMI on the Z3660",
	"Z3660 HDMI driver v1.0",
	0,255,
	"hdmi",
	NULL,
	HDMI_IsPresent,
	VC_SampleLoad,
	VC_SampleUnload,
	VC_SampleSpace,
	VC_SampleLength,
	HDMI_Init,
	HDMI_Exit,
	NULL,
	VC_SetNumVoices,
	VC_PlayStart,
	VC_PlayStop,
	HDMI_Update,
	NULL,
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

MISSING(drv_hdmi);

#endif

/* ex:set ts=4: */
