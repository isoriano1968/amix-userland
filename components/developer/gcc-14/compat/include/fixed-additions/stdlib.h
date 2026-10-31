/* Appended to GCC's fixed copy of AMIX's <stdlib.h>.
 *
 * alloca is declared nowhere in AMIX -- there is no <alloca.h> and neither
 * <stdlib.h> nor <malloc.h> mentions it -- yet it is ordinary in code of the
 * era and OpenTTD uses it.  GCC has always had the builtin, and mapping the
 * name onto it is what glibc, the BSDs and every other modern system do; there
 * is no library function to link against.
 *
 * A macro rather than a declaration on purpose: alloca must be expanded in the
 * caller's frame, so it cannot be a real function call.
 */
#ifndef _AMIX_HAVE_ALLOCA
#define _AMIX_HAVE_ALLOCA
#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif
#endif
