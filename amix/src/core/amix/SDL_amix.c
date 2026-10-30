/*
  Simple DirectMedia Layer: what AMIX (Amiga UNIX) lacks.
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.
*/
#include "../../SDL_internal.h"

#include <string.h>
#include <sys/utsname.h>

/* AMIX has no gethostname(): the node name from uname() */
int SDL_AMIX_gethostname(char *name, int len)
{
    struct utsname u;

    if (len <= 0 || uname(&u) < 0) {
        return -1;
    }
    strncpy(name, u.nodename, (size_t)len - 1);
    name[len - 1] = '\0';
    return 0;
}
