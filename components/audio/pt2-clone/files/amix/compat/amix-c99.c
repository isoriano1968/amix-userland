/*
 * amix-c99.c -- C library functions pt2-clone needs that AMIX (SVR4.0) libc
 * does not have; linked into the program so nothing is left for the dynamic
 * linker to look for at run time.
 */
#include <ctype.h>
#include <stddef.h>

int strncasecmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
		if (ca != cb)
			return ca - cb;
		if (!ca)
			break;
	}
	return 0;
}

int strcasecmp(const char *a, const char *b)
{
	return strncasecmp(a, b, (size_t)-1);
}
