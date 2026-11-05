#ifndef AMIX_PYTHON_COMPAT_H
#define AMIX_PYTHON_COMPAT_H
/* Private compatibility declarations; never installed over vendor headers. */
/* This port uses CPython's own UTF-8 locale conversion path. AMIX has no
 * nl_langinfo(CODESET); this does not change libc's locale or mbstowcs(). */
#define _Py_FORCE_UTF8_LOCALE 1
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <sys/types.h>
/* AMIX's sys/socket.h and sys/mman.h have the constants and structures but
 * declare none of the functions (libsocket.so, libc.so.1); GCC 14 refuses
 * implicit declarations.  Declared before pthread.h, which then maps connect,
 * accept, recv, send, recvfrom and sendto to Pth's yielding versions. */
struct sockaddr;
struct msghdr;
extern int socket(int, int, int);
extern int socketpair(int, int, int, int *);
extern int bind(int, const struct sockaddr *, int);
extern int connect(int, const struct sockaddr *, int);
extern int listen(int, int);
extern int accept(int, struct sockaddr *, int *);
extern int recv(int, void *, int, int);
extern int recvfrom(int, void *, int, int, struct sockaddr *, int *);
extern int recvmsg(int, struct msghdr *, int);
extern int send(int, const void *, int, int);
extern int sendto(int, const void *, int, int, const struct sockaddr *, int);
extern int sendmsg(int, const struct msghdr *, int);
extern int getsockopt(int, int, int, void *, int *);
extern int setsockopt(int, int, int, const void *, int);
extern int getsockname(int, struct sockaddr *, int *);
extern int getpeername(int, struct sockaddr *, int *);
extern int shutdown(int, int);
extern int munmap(void *, size_t);
extern int msync(void *, size_t, int);
extern int mprotect(void *, size_t, int);
/* libnsl.so */
extern int gethostname(char *, int);
/* syslog.o of libc's static part; syslog.h declares nothing */
extern void openlog(const char *, int, int);
extern void syslog(int, const char *, ...);
extern void closelog(void);
extern int setlogmask(int);
/* IPv4 only, ports/python/amix-inet.c: AMIX has inet_addr/inet_ntoa only;
 * CPython's getaddrinfo.c/getnameinfo.c fallbacks need these */
extern int inet_pton(int, const char *, void *);
extern const char *inet_ntop(int, const void *, char *, int);
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
/* SVR4 spells it _SC_PAGESIZE (sys/unistd.h) */
#ifndef _SC_PAGE_SIZE
#define _SC_PAGE_SIZE _SC_PAGESIZE
#endif
#include <pthread.h>
#include <sys/resource.h>
/* CPython's generated pyconfig.h spells this as 1. */
#undef _POSIX_THREADS
#define _POSIX_THREADS 1
#ifndef SSIZE_MAX
#define SSIZE_MAX __INT_MAX__
#endif
#ifndef isfinite
#define isfinite(x) __builtin_isfinite(x)
#endif
#ifndef isinf
#define isinf(x) __builtin_isinf(x)
#endif
#ifndef isnan
#define isnan(x) __builtin_isnan(x)
#endif
#ifndef NAN
#define NAN __builtin_nanf("")
#endif
#ifndef INFINITY
#define INFINITY __builtin_inff()
#endif
extern double expm1(double);
extern double log1p(double);
extern double log2(double);
extern double copysign(double, double);
extern double round(double);
extern double exp2(double);
extern double nextafter(double, double);
extern double acosh(double);
extern double asinh(double);
extern double atanh(double);
extern double erf(double);
extern double erfc(double);
extern double cbrt(double);
extern int amix_python_monotonic(long long *);
extern long amix_python_clock_hz(void);
extern struct tm *localtime_r(const time_t *, struct tm *);
extern struct tm *gmtime_r(const time_t *, struct tm *);
extern int getrlimit(int, struct rlimit *);
extern int setrlimit(int, const struct rlimit *);
extern int fchmod(int, mode_t);
extern int seteuid(uid_t);
extern int setegid(gid_t);
extern int grantpt(int);
extern int unlockpt(int);
extern char *ptsname(int);
extern int ttyname_r(int, char *, size_t);
extern int setenv(const char *, const char *, int);
extern int unsetenv(const char *);
#endif
