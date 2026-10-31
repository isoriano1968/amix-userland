/* amix_str.c -- the case-insensitive string comparisons AMIX does not have.
 *
 * strcasecmp and strncasecmp came from BSD and reached POSIX in 2001; SVR4.0
 * has neither, in libc.a or in libc.so -- checked by linking, not by reading a
 * header, because AMIX's headers and its library disagree often enough that
 * only the linker settles it.
 *
 * ASCII only, deliberately: tolower() in a C locale is what every caller here
 * means, and going through <ctype.h> would make the result depend on a locale
 * this target barely has.
 */
#include <stddef.h>

#include "amix_compat.h"

static int amix_lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int strcasecmp(const char *a, const char *b)
{
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;
	int d;

	for (;;) {
		d = amix_lower(*p) - amix_lower(*q);
		if (d != 0 || *p == '\0') return d;
		p++; q++;
	}
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;
	int d;

	while (n-- > 0) {
		d = amix_lower(*p) - amix_lower(*q);
		if (d != 0 || *p == '\0') return d;
		p++; q++;
	}
	return 0;
}

/* strsignal -- POSIX 2001, and absent from AMIX.  AMIX does carry the table it
 * is built from, _sys_siglist[], declared in <signal.h> and NSIG entries long,
 * so this is a bounds check over the real thing rather than a table of our own
 * invention that would drift from the kernel's numbering.
 *
 * Wanted by any program with a crash handler; OpenTTD's crashlog_unix.cpp uses
 * it for one line of text.
 */
#include <signal.h>

extern char *_sys_siglist[];

char *strsignal(int sig)
{
	if (sig > 0 && sig < NSIG && _sys_siglist[sig] != 0) return _sys_siglist[sig];
	return (char *)"unknown signal";
}
