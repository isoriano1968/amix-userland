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
