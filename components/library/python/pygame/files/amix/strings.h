/* strings.h for AMIX: SVR4.0 has none.  Inline, so that no module needs
 * the non-PIC libamixc99 */
#ifndef AMIX_STRINGS_H
#define AMIX_STRINGS_H
#include <stddef.h>
#include <ctype.h>
static inline int amix_strncasecmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
		if (d || !*a) return d;
	}
	return 0;
}
#define strncasecmp(a, b, n) amix_strncasecmp(a, b, n)
#define strcasecmp(a, b) amix_strncasecmp(a, b, (size_t)-1)
#endif
