/* c1_compat.h -- included first into every emulator file built for core 1 */
#ifndef C1_COMPAT_H
#define C1_COMPAT_H
#include <time.h>
#include <sys/stat.h>
#include <stddef.h>
/* PATH_MAX (the emulator's MAX_DPATH) from newlib itself: Debian's
   gcc-arm-none-eabi has GCC's own limits.h, which does not go on to newlib's,
   so <limits.h> alone has no PATH_MAX there; both toolchains give 1024 here */
#include <sys/syslimits.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4
#endif
int clock_gettime(clockid_t, struct timespec *);
int usleep(useconds_t);
unsigned sleep(unsigned);
int lstat(const char *, struct stat *);
void *valloc(size_t);
char *strcasestr(const char *, const char *);
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat
#define off64_t off_t
#define fseeko64 fseeko
#define ftello64 ftello
#define fopen64 fopen
#ifdef __cplusplus
}
#endif
#endif
