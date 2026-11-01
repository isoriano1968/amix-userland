/* SDL.h -- the handful of SDL calls ioquake3's sys_main.c makes, for AMIX,
   which has no SDL: video and input are amix_glimp.c and amix_input.c. */
#ifndef AMIX_SDL_H
#define AMIX_SDL_H
#include <stdlib.h>
typedef struct { unsigned char major, minor, patch; } SDL_version;
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 32
#define SDL_PATCHLEVEL 8
#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))
#define SDL_VERSION(x) ((x)->major = 2, (x)->minor = 32, (x)->patch = 8)
#define SDL_INIT_VIDEO 0x20
#define SDL_INIT_AUDIO 0x10
static __inline__ void SDL_GetVersion(SDL_version *v) { SDL_VERSION(v); }
static __inline__ int SDL_WasInit(unsigned int f) { (void) f; return 1; }
static __inline__ char *SDL_GetClipboardText(void) { return NULL; }
static __inline__ void SDL_free(void *p) { free(p); }
static __inline__ void SDL_Quit(void) { }
extern char *dlerror(void);
static __inline__ const char *SDL_GetError(void) { const char *e = dlerror(); return e ? e : "no SDL on AMIX"; }
static __inline__ int SDL_HasRDTSC(void) { return 0; }
static __inline__ int SDL_Has3DNow(void) { return 0; }
static __inline__ int SDL_HasMMX(void) { return 0; }
static __inline__ int SDL_HasSSE(void) { return 0; }
static __inline__ int SDL_HasSSE2(void) { return 0; }
static __inline__ int SDL_HasAltiVec(void) { return 0; }
#endif
