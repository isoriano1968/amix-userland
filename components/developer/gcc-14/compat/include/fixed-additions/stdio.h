/* Appended to GCC's fixed copy of AMIX's <stdio.h>.
 *
 * snprintf and vsnprintf are absent from AMIX entirely -- confirmed by the
 * native linker on the machine, not merely by reading the header.  They are
 * defined in libamixc99, which every link gets through the compiler's lib spec,
 * so declaring them here is what makes ordinary portable code compile
 * unchanged.  OpenTTD is the immediate case: it calls snprintf in a hundred
 * places and has no notion that a Unix might not have it.
 *
 * Carries its own extern "C" guard rather than relying on the wrapper around
 * the rest of the file: the wrapper step only wraps headers that do not already
 * have one, so by the time this text is appended the guard is closed above it.
 */
#ifndef _AMIX_HAVE_SNPRINTF
#define _AMIX_HAVE_SNPRINTF
#include <stdarg.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
#ifdef __cplusplus
}
#endif
#endif
