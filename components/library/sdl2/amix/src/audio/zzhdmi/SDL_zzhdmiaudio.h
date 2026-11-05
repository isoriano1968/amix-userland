/*
  Simple DirectMedia Layer: sound on the Z3660's HDMI (AMIX), "zzhdmi".
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.
*/
#include "../../SDL_internal.h"

#ifndef SDL_zzhdmiaudio_h_
#define SDL_zzhdmiaudio_h_

#include "../SDL_sysaudio.h"

#define _THIS SDL_AudioDevice *this

struct SDL_PrivateAudioData
{
    Uint8 *mixbuf;                  /* what SDL fills: 16-bit stereo, big-endian */
    unsigned long long mixed;       /* stream bytes written up to (their time) */
    unsigned long lead;             /* bytes kept written ahead of what is heard */
    unsigned long safe;             /* behind this, core 1 has read past us */
    unsigned long bytes_per_ms;
    unsigned long underruns;
    int opened;
};

#endif /* SDL_zzhdmiaudio_h_ */
