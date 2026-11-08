/* SDL.h -- what client/cl_main.c asks of SDL through sys/sys_loadlib.h
   (no dlopen on core 1: everything is linked into the image) */
#ifndef C1_SDL_H
#define C1_SDL_H
#include "SDL_version.h"
#define SDL_LoadObject(f)       ((void *)0)
#define SDL_LoadFunction(h, s)  ((void *)0)
#define SDL_UnloadObject(h)     ((void)(h))
#define SDL_GetError()          "no shared objects on core 1"
#endif
