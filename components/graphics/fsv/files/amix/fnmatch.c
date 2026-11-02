/* fnmatch.c -- POSIX fnmatch() for AMIX: *, ?, [...] with ranges and !,
 * backslash escapes, FNM_PATHNAME and FNM_PERIOD */
#include "fnmatch.h"

static int
match( const char *p, const char *s, int flags, int start )
{
	const char *q;
	int neg, ok;

	for (;; p++, s++) {
		if (*s == '.' && (flags & FNM_PERIOD) &&
		    (start || ((flags & FNM_PATHNAME) && s[-1] == '/')) && *p != '.')
			return FNM_NOMATCH;
		start = 0;
		switch (*p) {
		case '\0':
			return *s == '\0' ? 0 : FNM_NOMATCH;
		case '?':
			if (*s == '\0' || (*s == '/' && (flags & FNM_PATHNAME)))
				return FNM_NOMATCH;
			break;
		case '*':
			while (p[1] == '*')
				p++;
			for (q = s; ; q++) {
				if (match( p + 1, q, flags & ~FNM_PERIOD, 0 ) == 0)
					return 0;
				if (*q == '\0' || (*q == '/' && (flags & FNM_PATHNAME)))
					return FNM_NOMATCH;
			}
		case '[':
			if (*s == '\0' || (*s == '/' && (flags & FNM_PATHNAME)))
				return FNM_NOMATCH;
			q = p + 1;
			neg = (*q == '!' || *q == '^');
			if (neg)
				q++;
			ok = 0;
			do {
				if (q[1] == '-' && q[2] != ']' && q[2] != '\0') {
					if (*s >= q[0] && *s <= q[2])
						ok = 1;
					q += 3;
				}
				else {
					if (*s == *q)
						ok = 1;
					q++;
				}
			} while (*q != ']' && *q != '\0');
			if (*q == '\0' || ok == neg)
				return FNM_NOMATCH;
			p = q;
			break;
		case '\\':
			if (!(flags & FNM_NOESCAPE) && p[1] != '\0')
				p++;
			/* fall through */
		default:
			if (*p != *s)
				return FNM_NOMATCH;
		}
	}
}

int
fnmatch( const char *pattern, const char *string, int flags )
{
	return match( pattern, string, flags, 1 );
}
