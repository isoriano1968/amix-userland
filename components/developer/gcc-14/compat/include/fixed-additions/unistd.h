/* Appended to GCC's fixed copy of AMIX's <unistd.h>.
 *
 * AMIX sleeps in whole seconds and nothing else: sleep() is there, usleep() is
 * not.  libamixc99 implements it with select() and no descriptors, which is the
 * SVR4 way to wait for less than a second -- poll() would round to
 * milliseconds.  Anything with a frame loop needs this.
 *
 * Carries its own linkage guard, for the reason given in the stdio.h addition.
 */
#ifndef _AMIX_HAVE_USLEEP
#define _AMIX_HAVE_USLEEP
#ifdef __cplusplus
extern "C" {
#endif
int usleep(unsigned long);
#ifdef __cplusplus
}
#endif
#endif
