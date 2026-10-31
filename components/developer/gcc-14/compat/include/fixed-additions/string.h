/* Appended to GCC's fixed copy of AMIX's <string.h>.
 *
 * strcasecmp and strncasecmp came from BSD and reached POSIX in 2001.  AMIX
 * has neither -- verified by linking, since its headers and its library
 * disagree often enough that only the linker settles it.  libamixc99 defines
 * them; this makes them visible.
 *
 * Carries its own linkage guard, for the reason given in the stdio.h addition.
 */
#ifndef _AMIX_HAVE_STRCASECMP_AND_STRSIGNAL
#define _AMIX_HAVE_STRCASECMP_AND_STRSIGNAL
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
/* POSIX 2001; AMIX has the _sys_siglist table it is built from but not the
 * function. */
char *strsignal(int);
#ifdef __cplusplus
}
#endif
#endif
