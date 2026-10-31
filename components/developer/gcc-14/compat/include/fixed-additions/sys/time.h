/* Appended to GCC's fixed copy of AMIX's <sys/time.h>.
 *
 * gettimeofday is in libc and mentioned in this header only in a COMMENT --
 * "Structure returned by gettimeofday(2) system call" -- with no declaration
 * anywhere.  Anything that wants a clock finer than a second needs it, and on
 * this toolchain an implicit declaration is an error rather than a warning, so
 * it fails loudly instead of defaulting the return type to int.
 *
 * The second argument is void * rather than struct timezone *: SVR4 ignores it,
 * every caller passes NULL, and naming a type this header does not define would
 * only create a second problem.
 */
#ifndef _AMIX_HAVE_GETTIMEOFDAY
#define _AMIX_HAVE_GETTIMEOFDAY
#ifdef __cplusplus
extern "C" {
#endif
struct timeval;
int gettimeofday(struct timeval *, void *);
#ifdef __cplusplus
}
#endif
#endif
