/* SDL.h -- AMIX has no SDL; only Blender's sequencer audio (seqaudio.c) uses
 * it.  Every call reports "no audio device", so the sequencer runs silent. */
#ifndef AMIX_SDL_STUB_H
#define AMIX_SDL_STUB_H
#include <stdint.h>
#define AUDIO_S16SYS 0x9010
typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef struct SDL_AudioSpec {
    int freq;
    uint16_t format;
    uint8_t channels, silence;
    uint16_t samples, padding;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void *userdata;
} SDL_AudioSpec;
#define SDL_MIX_MAXVOLUME 128
static int  SDL_OpenAudio(SDL_AudioSpec *want, SDL_AudioSpec *have) { (void) want; (void) have; return -1; }
static void SDL_CloseAudio(void) { }
static void SDL_PauseAudio(int on) { (void) on; }
static void SDL_MixAudio(uint8_t *d, const uint8_t *s, uint32_t n, int v) { (void) d; (void) s; (void) n; (void) v; }
static const char *SDL_GetError(void) { return "no audio on AMIX"; }
#endif
