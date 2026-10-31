/* amix_compat.h -- what AMIX's SVR4.0 libc does not have.
 *
 * AMIX predates C99 by eight years, and its headers are additionally
 * incomplete against its own library. This declares the gaps a port has to
 * fill; amix_compat.c fills them.
 *
 * Verified absent on the real machine (AMIX 2.1c), not merely in an image: the
 * native linker rejects a program calling snprintf with
 *     Undefined  first referenced
 *      symbol     in file
 *     snprintf    /var/tmp/cca000Se1.o
 */
#ifndef AMIX_COMPAT_H
#define AMIX_COMPAT_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Absent from libc entirely. */
int amix_snprintf(char *str, size_t size, const char *fmt, ...);
int amix_vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int amix_usleep(unsigned long usec);

/* In libc, but with no prototype in AMIX's own <sys/time.h>, which declares
 * adjtime, getitimer and setitimer and stops there. */
struct timeval;
extern int gettimeofday(struct timeval *, void *);

#ifdef AMIX_COMPAT_TAKE_OVER_NAMES
/* Opt-in: let portable code call the standard names. OpenTTD's stdafx.h
 * already declares snprintf itself for platforms that lack it, so the port
 * defines this in one place rather than editing call sites. */
#define snprintf  amix_snprintf
#define vsnprintf amix_vsnprintf
#define usleep    amix_usleep
#endif

#ifdef __cplusplus
}
#endif

#endif /* AMIX_COMPAT_H */
