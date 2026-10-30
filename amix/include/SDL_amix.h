/*
 * SDL_amix.h -- forced into every SDL file for AMIX (after gnu-pth's
 * pthread.h).  What AMIX's headers and libraries lack.
 */
#ifndef SDL_AMIX_H
#define SDL_AMIX_H

/* AMIX has no gethostname(); src/core/amix/SDL_amix.c builds it on uname() */
int SDL_AMIX_gethostname(char *name, int len);
#define gethostname(n, l) SDL_AMIX_gethostname(n, l)

/* C99's isblank, not in AMIX's ctype.h */
#ifndef isblank
#define isblank(c) ((c) == ' ' || (c) == '\t')
#endif

/* src/render/zz9k: the renderer that draws with soft3D on the Z3660 */
#define SDL_VIDEO_RENDER_ZZ9K 1

#endif
