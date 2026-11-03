/* amix-netsurf.c -- what NetSurf calls and AMIX's libc lacks (amix-netsurf.h) */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

unsigned long long
strtoull(const char *s, char **end, int base)
{
	const char *p = s;
	unsigned long long v = 0;
	int neg = 0, any = 0, over = 0, d;

	while (isspace((unsigned char)*p))
		p++;
	if (*p == '+' || *p == '-')
		neg = *p++ == '-';
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
	    isxdigit((unsigned char)p[2])) {
		p += 2;
		base = 16;
	} else if (base == 0)
		base = *p == '0' ? 8 : 10;
	for (;; p++) {
		if (isdigit((unsigned char)*p))
			d = *p - '0';
		else if (isalpha((unsigned char)*p))
			d = tolower((unsigned char)*p) - 'a' + 10;
		else
			break;
		if (d >= base)
			break;
		any = 1;
		if (v > (~0ULL - d) / base)
			over = 1;
		else
			v = v * base + d;
	}
	if (end)
		*end = (char *)(any ? p : s);
	if (over) {
		errno = ERANGE;
		return ~0ULL;
	}
	return neg ? -v : v;
}

long long
strtoll(const char *s, char **end, int base)
{
	const char *p = s;
	unsigned long long v;
	int neg;

	while (isspace((unsigned char)*p))
		p++;
	neg = *p == '-';
	errno = 0;
	v = strtoull(s, end, base);
	if (errno == ERANGE)
		return neg ? LLONG_MIN : LLONG_MAX;
	if (neg) {
		v = -v;		/* strtoull negated it */
		if (v > (unsigned long long)LLONG_MAX + 1) {
			errno = ERANGE;
			return LLONG_MIN;
		}
		return -(long long)v;
	}
	if (v > LLONG_MAX) {
		errno = ERANGE;
		return LLONG_MAX;
	}
	return (long long)v;
}
