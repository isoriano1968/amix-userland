/* SDL_loadso.h -- SDL's shared-object calls on AMIX's dlopen */
#ifndef AMIX_SDL_LOADSO_H
#define AMIX_SDL_LOADSO_H
#include <dlfcn.h>
#define SDL_LoadObject(f)       dlopen((f), RTLD_NOW)
#define SDL_LoadFunction(h, s)  dlsym((h), (s))
#define SDL_UnloadObject(h)     dlclose(h)
#endif
