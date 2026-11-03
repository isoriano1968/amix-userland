/* amix-netsurf.h -- forced into every compile of NetSurf: what AMIX's
   1991 libc (SVR4.0) lacks of C99 and POSIX.1-2001.  Functions are in
   amix-netsurf.c. */
#ifndef AMIX_NETSURF_H
#define AMIX_NETSURF_H
#include <sys/types.h>		/* before sys/socket.h, which needs u_short */
#include <stdlib.h>
#include <errno.h>		/* utils.c's inet_pton fallback sets errno */
#include <math.h>

#define NO_IPV6			/* AMIX's TCP/IP has no IPv6 */

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2	1.57079632679489661923
#endif

/* the toolchain's inttypes.h has the d, u and x ones */
#ifndef PRIX32
#define PRIX32	"X"
#endif
#ifndef SCNx32
#define SCNx32	"x"
#endif

static inline float amix_strtof(const char *s, char **end) { return (float)strtod(s, end); }
#define strtof amix_strtof
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);

/* NetSurf's libutf8proc (1.3) names utf8proc 2's utf8proc_normalize_utf32 so */
#define utf8proc_normalize_utf32 utf8proc_normalise

#endif
