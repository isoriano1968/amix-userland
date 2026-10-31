/*
  Simple DirectMedia Layer: the AMIX (Amiga UNIX) Paula /dev/audio driver.
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.

  AMIX's /dev/audio gives each open() one of Paula's channels: signed 8-bit
  samples, the rate set as Paula's period (PAL clock / period, period 124 or
  more, so at most 28.6 kHz).  The 68040/68060 port kernels add AUDIO_STEREO,
  a sample-locked left/right pair on one descriptor with interleaved samples.
  There are no format ioctls: SDL converts the program's format to this one.

  The driver queues at most two writes, each held in a slot of 8 KB, so a
  write is kept to 4 KB.  The device is opened O_NDELAY: when it is full,
  write() takes nothing and the thread waits in SDL_Delay, which gnu-pth
  (AMIX's threads) turns into a switch to another thread, never a stall of
  the whole process -- for half a buffer's playing time, so the thread
  wakes about twice a buffer, not every 10 ms.  The plain write(), not
  pth's: that one costs a dozen system calls (fcntl, libsocket's I_FIND
  ioctls, poll) per write on a device that is non-blocking anyway.
*/
#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_AMIX

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "SDL_timer.h"
#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_amixaudio.h"

#undef write    /* the system call, not gnu-pth's __pthread_write (see the top) */

/* the AMIX audio driver's ioctls (usr/sys/amiga/driver/audio.h, port kernels) */
#define AUIOC           ('U' << 8)
#define AUDIO_VOLUME    (AUIOC | 1)
#define AUDIO_PERIOD    (AUIOC | 2)
#define AUDIO_STEREO    (AUIOC | 3)
#define PAULA_PAL_CLOCK 3546895
#define PAULA_MIN_PERIOD 124
#define MAX_WRITE       4096    /* under the driver's 8 KB slot */
#define MIN_WRITE       2048

static void AMIXAUDIO_PlayDevice(_THIS)
{
    const Uint8 *p = this->hidden->mixbuf;
    int left = this->spec.size, n;

    while (left > 0 && SDL_AtomicGet(&this->enabled)) {
        n = write(this->hidden->audio_fd, p, left > MAX_WRITE ? MAX_WRITE : left);
        if (n > 0) {
            p += n;
            left -= n;
        } else if (n == 0 || errno == EAGAIN || errno == EINTR) {
            SDL_Delay(this->hidden->wait_ms);   /* full: let the other threads run */
        } else {
            SDL_OpenedAudioDeviceDisconnected(this);
            return;
        }
    }
}

static Uint8 *AMIXAUDIO_GetDeviceBuf(_THIS)
{
    return this->hidden->mixbuf;
}

static void AMIXAUDIO_CloseDevice(_THIS)
{
    if (this->hidden->audio_fd >= 0) {
        close(this->hidden->audio_fd);
    }
    SDL_free(this->hidden->mixbuf);
    SDL_free(this->hidden);
}

static int AMIXAUDIO_OpenDevice(_THIS, const char *devname)
{
    int period, rate, vol = 63, one = 1;

    (void)devname;
    if (this->iscapture) {
        return SDL_SetError("AMIX audio: no capture");
    }
    this->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*this->hidden));
    if (!this->hidden) {
        return SDL_OutOfMemory();
    }
    this->hidden->audio_fd = open("/dev/audio", O_WRONLY | O_NDELAY);
    if (this->hidden->audio_fd < 0) {
        return SDL_SetError("Couldn't open /dev/audio: %s", strerror(errno));
    }

    /* stereo through the kernel's locked pair, if it has one and it is wanted */
    if (this->spec.channels >= 2 && ioctl(this->hidden->audio_fd, AUDIO_STEREO, &one) == 0) {
        this->spec.channels = 2;
    } else {
        this->spec.channels = 1;
    }

    /* the Paula period nearest the rate asked for */
    period = this->spec.freq > 0 ? (PAULA_PAL_CLOCK + this->spec.freq / 2) / this->spec.freq : 128;
    if (period < PAULA_MIN_PERIOD) {
        period = PAULA_MIN_PERIOD;
    }
    if (ioctl(this->hidden->audio_fd, AUDIO_PERIOD, &period) < 0 ||
        ioctl(this->hidden->audio_fd, AUDIO_VOLUME, &vol) < 0) {
        return SDL_SetError("/dev/audio ioctl: %s", strerror(errno));
    }
    /*
     * Paula's rate, unless it is within 0.5% of the rate asked for: then the
     * one asked for, so the program (ffplay) does not resample 22050 Hz to
     * 22030 Hz on a 68060 for a difference no one hears.
     */
    rate = (PAULA_PAL_CLOCK + period / 2) / period;
    if (this->spec.freq <= 0 || SDL_abs(rate - this->spec.freq) * 200 > this->spec.freq) {
        this->spec.freq = rate;
    }
    this->spec.format = AUDIO_S8;

    /* a buffer of 2 KB (fewer wake-ups: ffplay asks for 1 KB at 22 kHz) to
       MAX_WRITE bytes, in whole frames */
    if (this->spec.samples == 0 || this->spec.samples * this->spec.channels > MAX_WRITE) {
        this->spec.samples = MAX_WRITE / this->spec.channels;
    } else if (this->spec.samples * this->spec.channels < MIN_WRITE) {
        this->spec.samples = MIN_WRITE / this->spec.channels;
    }
    SDL_CalculateAudioSpec(&this->spec);
    this->hidden->wait_ms = this->spec.samples * 500 / this->spec.freq;  /* half a buffer */
    if (this->hidden->wait_ms < 5) {
        this->hidden->wait_ms = 5;
    }

    this->hidden->mixbuf = (Uint8 *)SDL_malloc(this->spec.size);
    if (!this->hidden->mixbuf) {
        return SDL_OutOfMemory();
    }
    SDL_memset(this->hidden->mixbuf, this->spec.silence, this->spec.size);
    return 0;
}

static SDL_bool AMIXAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = AMIXAUDIO_OpenDevice;
    impl->PlayDevice = AMIXAUDIO_PlayDevice;
    impl->GetDeviceBuf = AMIXAUDIO_GetDeviceBuf;
    impl->CloseDevice = AMIXAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap AMIXAUDIO_bootstrap = {
    "amix", "AMIX Paula /dev/audio", AMIXAUDIO_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_AMIX */
