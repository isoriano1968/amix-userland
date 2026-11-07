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

/*
 * MIT-SHM: shmget, shmat, shmdt and shmctl as libc.a's shmsys.o makes them,
 * system call 52 (shmsys) with the operation first, but PIC and hidden, so
 * that libSDL2.so needs nothing from the program.  The kernel reads the
 * arguments above a return address; carry set means d0 is an errno.
 */
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

static long amix_shmsys(long op, long a1, long a2, long a3)
{
    long r;
    char err;

    __asm__ __volatile__(
        "move.l %5,-(%%sp)\n\t"
        "move.l %4,-(%%sp)\n\t"
        "move.l %3,-(%%sp)\n\t"
        "move.l %2,-(%%sp)\n\t"
        "clr.l -(%%sp)\n\t"
        "moveq #52,%%d0\n\t"
        "trap #0\n\t"
        "scs %1\n\t"
        "lea 20(%%sp),%%sp\n\t"
        "move.l %%d0,%0"
        : "=&d"(r), "=&d"(err)
        : "r"(op), "r"(a1), "r"(a2), "r"(a3)
        : "d0", "d1", "a0", "a1", "cc", "memory");
    if (err) {
        errno = (int)r;
        return -1;
    }
    return r;
}

int shmget(key_t key, int size, int flags)
{
    return (int)amix_shmsys(3, (long)key, size, flags);
}

void *shmat(int id, void *addr, int flags)
{
    return (void *)amix_shmsys(0, id, (long)addr, flags);
}

int shmdt(void *addr)
{
    return (int)amix_shmsys(2, (long)addr, 0, 0);
}

int shmctl(int id, int cmd, ...)
{
    va_list ap;
    struct shmid_ds *buf;

    va_start(ap, cmd);
    buf = va_arg(ap, struct shmid_ds *);
    va_end(ap);
    return (int)amix_shmsys(1, id, cmd, (long)buf);
}
