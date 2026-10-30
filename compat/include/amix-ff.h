/*
 * amix-ff.h -- forced into every FFmpeg compile (-include).  What AMIX
 * lacks beyond what the toolchain's compat layer provides.
 */
#ifndef AMIX_FF_H
#define AMIX_FF_H

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/resource.h>

/*
 * sys/signal.h defines sigset_t only for _POSIX_SOURCE (gcc's __STDC__ is
 * always 1), while signal.h declares sigprocmask and friends for
 * _XOPEN_SOURCE too.  Read it once with _POSIX_SOURCE; its guard keeps
 * later includes from reading it again.
 */
#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#include <sys/signal.h>
#undef _POSIX_SOURCE
#else
#include <sys/signal.h>
#endif

/*
 * ffplay is built with gnu-pth's pthread.h, which maps read and write to
 * pth's wrappers: per call an fcntl to read the flags, one to make the fd
 * non-blocking, a poll, the call, an fcntl to restore -- and libsocket's
 * fcntl adds an I_FIND "sockmod" ioctl to each.  FFmpeg reads regular files
 * here, which never block for long, so it gets the plain system calls.
 * (The threads still switch in pth's mutexes, conditions and sleeps.)
 */
#ifdef read
#undef read
#undef write
#undef readv
#undef writev
#endif

#ifndef MAP_FAILED
#define MAP_FAILED	((void *)-1)
#endif

long long llabs(long long);
/* in libc, but stdlib.h hides it when _XOPEN_SOURCE is defined */
void *memalign(size_t, size_t);
/* in libc, declared in no AMIX header */
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
double scalbn(double, int);

/*
 * compat/amix-math.c: rounding and square roots with the 68060's FPU
 * instructions.  AMIX's libm has no lrint/round/trunc and does floor, ceil
 * and sqrt without the FPU (2.5-5.8 us); math.h first, then every call in
 * FFmpeg goes to amix_*.
 */
#include <math.h>
double amix_rint(double);
float amix_rintf(float);
double amix_trunc(double);
float amix_truncf(float);
double amix_floor(double);
float amix_floorf(float);
double amix_ceil(double);
float amix_ceilf(float);
double amix_round(double);
float amix_roundf(float);
long amix_lrint(double);
long amix_lrintf(float);
long long amix_llrint(double);
long long amix_llrintf(float);
double amix_sqrt(double);
float amix_sqrtf(float);
#define rint(x)		amix_rint(x)
#define rintf(x)	amix_rintf(x)
#define trunc(x)	amix_trunc(x)
#define truncf(x)	amix_truncf(x)
#define floor(x)	amix_floor(x)
#define floorf(x)	amix_floorf(x)
#define ceil(x)		amix_ceil(x)
#define ceilf(x)	amix_ceilf(x)
#define round(x)	amix_round(x)
#define roundf(x)	amix_roundf(x)
#define lrint(x)	amix_lrint(x)
#define lrintf(x)	amix_lrintf(x)
#define llrint(x)	amix_llrint(x)
#define llrintf(x)	amix_llrintf(x)
#define sqrt(x)		amix_sqrt(x)
#define sqrtf(x)	amix_sqrtf(x)

/* network: AMIX's sys/socket.h and sys/poll.h give no prototypes, nfds_t or socklen_t */
#include <sys/types.h>
typedef unsigned long nfds_t;
typedef int socklen_t;
struct sockaddr;
extern int socket(int, int, int);
extern int bind(int, const struct sockaddr *, int);
extern int listen(int, int);
#ifndef accept
extern int accept(int, struct sockaddr *, int *);
#endif
#ifdef connect
#undef connect                   /* gnu-pth's wrapper has the same trouble */
#else
extern int connect(int, const struct sockaddr *, int);
#endif
/* poll() calls a non-blocking connect done before it is: blocking instead */
extern int amix_connect(int, const struct sockaddr *, int);
#define connect(s, a, l) amix_connect(s, a, l)
extern int setsockopt(int, int, int, const void *, int);
extern int getsockopt(int, int, int, void *, int *);
extern int getsockname(int, struct sockaddr *, int *);
extern int getpeername(int, struct sockaddr *, int *);
extern int shutdown(int, int);
#ifndef send
extern int send(int, const void *, int, int);
extern int recv(int, void *, int, int);
#endif

#endif
