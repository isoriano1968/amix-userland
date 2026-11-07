/*
 * SDL_amix.h -- forced into every SDL file for AMIX (after gnu-pth's
 * pthread.h).  What AMIX's headers and libraries lack.
 */
#ifndef SDL_AMIX_H
#define SDL_AMIX_H

/* AMIX has no gethostname(); src/core/amix/SDL_amix.c builds it on uname() */
int SDL_AMIX_gethostname(char *name, int len);
#define gethostname(n, l) SDL_AMIX_gethostname(n, l)

/* MIT-SHM (src/video/x11): libc.so.1 has only shmat, and libc.a's shm*
   wrappers are not PIC; src/core/amix/SDL_amix.c makes the shmsys calls */
#define shmget SDL_AMIX_shmget
#define shmat  SDL_AMIX_shmat
#define shmdt  SDL_AMIX_shmdt
#define shmctl SDL_AMIX_shmctl

/* C99's isblank, not in AMIX's ctype.h */
#ifndef isblank
#define isblank(c) ((c) == ' ' || (c) == '\t')
#endif

/* src/render/zz9k: the renderer that draws with soft3D on the Z3660 */
#define SDL_VIDEO_RENDER_ZZ9K 1

#endif
