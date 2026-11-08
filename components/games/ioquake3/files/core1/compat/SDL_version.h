/* SDL_version.h -- client/cl_main.c includes sys/sys_local.h, which asks
   for SDL's version; core 1 has no SDL (its platform is files/core1) */
#ifndef C1_SDL_VERSION_H
#define C1_SDL_VERSION_H
typedef struct { unsigned char major, minor, patch; } SDL_version;
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 32
#define SDL_PATCHLEVEL 8
#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))
#define SDL_VERSION(x) ((x)->major = 2, (x)->minor = 32, (x)->patch = 8)
#endif
