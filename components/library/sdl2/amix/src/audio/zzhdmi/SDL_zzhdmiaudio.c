/*
  Simple DirectMedia Layer: sound on the Z3660's HDMI (AMIX), "zzhdmi".
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.

  The program's audio goes, as 16-bit big-endian stereo at 24 kHz (48 kHz
  when it asks for 36 kHz or more), into the stream in board memory that
  core 1's GL server plays on the HDMI (zzhdmi_board.c, as libmikmod's
  drv_hdmi).  The stream is indexed by time: a buffer is written at the
  offset of the time it is to be heard, and the audio thread keeps about
  SDL_ZZHDMI_LEAD_MS (default 300) written ahead of what the HDMI has
  played.  Core 1 reads 120 ms ahead itself; when the thread falls behind
  that (the 68060 busy elsewhere: gnu-pth switches threads only when one
  waits), the writing jumps forward to the present over silence.

  Listed before "amix" (Paula): SDL takes this one when core 1 runs the GL
  server's program and no other program plays through it; else Paula.
  SDL_AUDIODRIVER=amix plays on Paula.
*/
#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_ZZHDMI

#include "SDL_timer.h"
#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_zzhdmiaudio.h"
#include "zzhdmi_board.h"

#define MIN_SAMPLES 512
#define MAX_SAMPLES 4096
#define SAFE_MS     (ZZHDMI_CORE1_LEAD_MS + 30)     /* written at least this far ahead */

static void ZZHDMI_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *h = this->hidden;

    while (SDL_AtomicGet(&this->enabled)) {
        const long long ahead = (long long)(h->mixed - zzhdmi_pos());
        long ms;

        if (ahead < (long long)h->lead) {
            return;
        }
        ms = (long)((ahead - (long long)h->lead) / h->bytes_per_ms) + 1;
        SDL_Delay(ms > 50 ? 50 : (Uint32)ms);       /* gnu-pth: the other threads run */
    }
}

static void ZZHDMI_PlayDevice(_THIS)
{
    struct SDL_PrivateAudioData *h = this->hidden;
    const unsigned long long now = zzhdmi_pos();

    if (h->mixed < now + h->safe) {
        /* behind core 1: on from the present, over silence (the whole ring:
           what core 1 meets there now is a lap or more old) */
        zzhdmi_silence(0, ZZHDMI_RING_BYTES);
        h->mixed = (now + h->lead) & ~3ULL;
        h->underruns++;
    }
    zzhdmi_write(h->mixed, h->mixbuf, this->spec.size);
    h->mixed += this->spec.size;
}

static Uint8 *ZZHDMI_GetDeviceBuf(_THIS)
{
    return this->hidden->mixbuf;
}

static void ZZHDMI_CloseDevice(_THIS)
{
    if (this->hidden->opened) {
        zzhdmi_close();
    }
    if (this->hidden->underruns && SDL_getenv("SDL_ZZHDMI_DEBUG")) {
        SDL_Log("zzhdmi: %lu underruns", this->hidden->underruns);
    }
    SDL_free(this->hidden->mixbuf);
    SDL_free(this->hidden);
}

static int ZZHDMI_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *h;
    const char *e = SDL_getenv("SDL_ZZHDMI_LEAD_MS");
    int div, lead_ms = e ? SDL_atoi(e) : 300;

    (void)devname;
    if (this->iscapture) {
        return SDL_SetError("zzhdmi: no capture");
    }
    h = this->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*h));
    if (!h) {
        return SDL_OutOfMemory();
    }
    div = this->spec.freq >= 36000 ? 1 : 2;
    this->spec.freq = 48000 / div;
    this->spec.format = AUDIO_S16MSB;
    this->spec.channels = 2;
    if (this->spec.samples < MIN_SAMPLES) {
        this->spec.samples = MIN_SAMPLES;
    } else if (this->spec.samples > MAX_SAMPLES) {
        this->spec.samples = MAX_SAMPLES;
    }
    SDL_CalculateAudioSpec(&this->spec);

    h->bytes_per_ms = (unsigned long)this->spec.freq * 4 / 1000;
    if (lead_ms < SAFE_MS + 50) {
        lead_ms = SAFE_MS + 50;
    } else if (lead_ms > 1000) {
        lead_ms = 1000;                 /* the ring holds 1.4 s at 24 kHz, 0.7 at 48 */
    }
    h->lead = h->bytes_per_ms * (unsigned long)lead_ms;
    if (h->lead + this->spec.size > ZZHDMI_RING_BYTES - h->bytes_per_ms * 50) {
        h->lead = ZZHDMI_RING_BYTES - h->bytes_per_ms * 50 - this->spec.size;
    }
    h->safe = h->bytes_per_ms * SAFE_MS;
    h->mixed = h->safe;                 /* the first buffer just ahead of core 1 */
    if (zzhdmi_open(div, h->lead + this->spec.size) < 0) {
        return SDL_SetError("zzhdmi: core 1's HDMI sound is not available");
    }
    h->opened = 1;

    h->mixbuf = (Uint8 *)SDL_malloc(this->spec.size);
    if (!h->mixbuf) {
        return SDL_OutOfMemory();
    }
    SDL_memset(h->mixbuf, this->spec.silence, this->spec.size);
    return 0;
}

static SDL_bool ZZHDMI_Init(SDL_AudioDriverImpl *impl)
{
    /* SDL takes the first driver whose Init succeeds: Paula when no core 1 */
    if (!zzhdmi_present()) {
        return SDL_FALSE;
    }
    impl->OpenDevice = ZZHDMI_OpenDevice;
    impl->WaitDevice = ZZHDMI_WaitDevice;
    impl->PlayDevice = ZZHDMI_PlayDevice;
    impl->GetDeviceBuf = ZZHDMI_GetDeviceBuf;
    impl->CloseDevice = ZZHDMI_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap ZZHDMIAUDIO_bootstrap = {
    "zzhdmi", "Z3660 HDMI through core 1", ZZHDMI_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_ZZHDMI */
