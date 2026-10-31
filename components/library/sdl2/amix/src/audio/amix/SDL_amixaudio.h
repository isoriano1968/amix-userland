/*
  Simple DirectMedia Layer: the AMIX (Amiga UNIX) Paula /dev/audio driver.
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.
*/
#include "../../SDL_internal.h"

#ifndef SDL_amixaudio_h_
#define SDL_amixaudio_h_

#include "../SDL_sysaudio.h"

#define _THIS SDL_AudioDevice *this

struct SDL_PrivateAudioData
{
    int audio_fd;       /* /dev/audio, O_NDELAY */
    Uint8 *mixbuf;      /* what SDL fills: signed 8-bit at Paula's rate */
    int wait_ms;        /* how long to wait when the device is full */
};

#endif /* SDL_amixaudio_h_ */
