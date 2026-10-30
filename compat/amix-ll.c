/*
 * strtoll and strtoull, which AMIX's libc lacks.  C99 semantics: leading
 * white space, an optional sign, base 0 (with 0 and 0x prefixes) or 2-36,
 * *endp set to the first unused character, ERANGE and the limit on
 * overflow.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>

#ifndef LLONG_MAX
#define LLONG_MAX	9223372036854775807LL
#define LLONG_MIN	(-LLONG_MAX - 1LL)
#define ULLONG_MAX	18446744073709551615ULL
#endif

static unsigned long long
amix_strtoull(const char *s, char **endp, int base, int *neg, int *ovf)
{
	const char *p = s, *digits;
	unsigned long long v = 0, cut;
	int d, cutd, any = 0;

	*neg = *ovf = 0;
	while (isspace((unsigned char)*p))
		p++;
	if (*p == '-' || *p == '+')
		*neg = *p++ == '-';
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
	    isxdigit((unsigned char)p[2])) {
		p += 2;
		base = 16;
	} else if (base == 0)
		base = *p == '0' ? 8 : 10;
	if (base < 2 || base > 36) {
		errno = EINVAL;
		if (endp)
			*endp = (char *)s;
		return 0;
	}
	cut = ULLONG_MAX / base;
	cutd = (int)(ULLONG_MAX % base);
	for (digits = p;; p++) {
		if (isdigit((unsigned char)*p))
			d = *p - '0';
		else if (isalpha((unsigned char)*p))
			d = tolower((unsigned char)*p) - 'a' + 10;
		else
			break;
		if (d >= base)
			break;
		any = 1;
		if (v > cut || (v == cut && d > cutd))
			*ovf = 1;
		else
			v = v * base + d;
	}
	if (endp)
		*endp = (char *)(any ? p : s);
	(void)digits;
	return v;
}

unsigned long long
strtoull(const char *s, char **endp, int base)
{
	int neg, ovf;
	unsigned long long v = amix_strtoull(s, endp, base, &neg, &ovf);

	if (ovf) {
		errno = ERANGE;
		return ULLONG_MAX;
	}
	return neg ? -v : v;
}

long long
strtoll(const char *s, char **endp, int base)
{
	int neg, ovf;
	unsigned long long v = amix_strtoull(s, endp, base, &neg, &ovf);

	if (!ovf && !neg && v <= (unsigned long long)LLONG_MAX)
		return (long long)v;
	if (!ovf && neg && v <= (unsigned long long)LLONG_MAX + 1ULL)
		return (long long)(0ULL - v);
	errno = ERANGE;
	return neg ? LLONG_MIN : LLONG_MAX;
}

long long
llabs(long long v)
{
	return v < 0 ? -v : v;
}
