/* config.h -- freeglut on AMIX by hand: SVR4, X11R6.3 without XInput2,
   Xrandr or XF86VidMode */
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_FCNTL_H 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_XPARSEGEOMETRY 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_ULONG_LONG 1
#define HAVE_VFPRINTF 1
#define HAVE_GL_GLU_H 0
#define VERSION_MAJOR 3
#define VERSION_MINOR 4
#define VERSION_PATCH 0

/* AMIX has neither; usleep is in the static part of libc, which a .so cannot use */
#include <sys/types.h>
#include <poll.h>
int gethostname();
#define timespec amix_timespec
struct amix_timespec { long tv_sec, tv_nsec; };
#define nanosleep(t, r) poll(0, 0, (int)((t)->tv_sec * 1000 + (t)->tv_nsec / 1000000))

/* select is in the static part of libc too: the one call in fg_main_x11.c, on
   one descriptor */
static int
amix_select(int n, fd_set *r, void *w, void *e, struct timeval *t)
{
   struct pollfd p;

   p.fd = n - 1;
   p.events = POLLIN;
   p.revents = 0;
   return poll(&p, 1, t ? (int)(t->tv_sec * 1000 + t->tv_usec / 1000) : -1);
}
#define select amix_select
