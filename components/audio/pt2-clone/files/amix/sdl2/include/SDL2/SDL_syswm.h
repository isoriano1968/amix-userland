/* SDL_syswm.h -- sdl2-amix: the window-manager info pt2-clone asks for (unused on X11/AMIX) */
#ifndef SDL_AMIX_SDL_SYSWM_H
#define SDL_AMIX_SDL_SYSWM_H
#include "SDL.h"
typedef enum { SDL_SYSWM_UNKNOWN, SDL_SYSWM_WINDOWS, SDL_SYSWM_X11 } SDL_SYSWM_TYPE;
struct SDL_SysWMmsg { SDL_version version; SDL_SYSWM_TYPE subsystem; int dummy; };
typedef struct SDL_SysWMinfo {
    SDL_version version; SDL_SYSWM_TYPE subsystem;
    union { struct { void *window; } win; struct { void *display; unsigned long window; } x11; Uint8 dummy[64]; } info;
} SDL_SysWMinfo;
SDL_bool SDL_GetWindowWMInfo(SDL_Window *window, SDL_SysWMinfo *info);
#endif
