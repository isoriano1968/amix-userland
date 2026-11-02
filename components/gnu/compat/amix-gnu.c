/* amix-gnu.c -- what current gnulib expects and a 1991 SVR4 libc has not */
#include <stdlib.h>
#ifndef AMIX_PROG
#define AMIX_PROG "gnu"
#endif

const char *
getprogname(void)
{
	return AMIX_PROG;
}

/* assert.h has only a macro, and not in every mode */
void
assert(int e)
{
	(void)e;
}

/* wide characters, in the C locale, which is all AMIX has */
#include <string.h>
#include <ctype.h>
typedef unsigned int wint_amix;

int
mbtowc_with_lock(wchar_t *p, const char *s, size_t n)
{
	return mbtowc(p, s, n);
}

size_t
wcslen(const wchar_t *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

wchar_t *
wcscat(wchar_t *d, const wchar_t *s)
{
	wchar_t *r = d;

	d += wcslen(d);
	while ((*d++ = *s++) != 0)
		;
	return r;
}

wchar_t *
wmemcpy(wchar_t *d, const wchar_t *s, size_t n)
{
	return memcpy(d, s, n * sizeof(wchar_t));
}

unsigned long
wctype(const char *name)
{
	static const char *const names[] = { "alnum", "alpha", "blank", "cntrl", "digit", "graph",
		"lower", "print", "punct", "space", "upper", "xdigit", 0 };
	unsigned long i;

	for (i = 0; names[i]; i++)
		if (!strcmp(name, names[i]))
			return i + 1;
	return 0;
}

int
iswctype(wint_amix c, unsigned long t)
{
	if (c > 255)
		return 0;
	switch (t) {
	case 1: return isalnum(c);
	case 2: return isalpha(c);
	case 3: return c == ' ' || c == '\t';
	case 4: return iscntrl(c);
	case 5: return isdigit(c);
	case 6: return isgraph(c);
	case 7: return islower(c);
	case 8: return isprint(c);
	case 9: return ispunct(c);
	case 10: return isspace(c);
	case 11: return isupper(c);
	case 12: return isxdigit(c);
	}
	return 0;
}

/* one byte, one character */
size_t
mbrtowc(wchar_t *p, const char *s, size_t n, void *st)
{
	if (!s)
		return 0;
	if (!n)
		return (size_t)-2;
	if (p)
		*p = (unsigned char)*s;
	return *s != 0;
}

size_t
wcrtomb(char *s, wchar_t c, void *st)
{
	if (s)
		*s = (char)c;
	return 1;
}

size_t
mbrlen(const char *s, size_t n, void *st)
{
	return mbrtowc(NULL, s, n, st);
}

int
mbsinit(const void *st)
{
	return 1;
}

int
wcwidth(wchar_t c)
{
	return c < 32 ? -1 : 1;
}

int
isblank(int c)
{
	return c == ' ' || c == '\t';
}

char *
nl_langinfo(int item)
{
	return "ANSI_X3.4-1968";
}

long long
strtoll(const char *s, char **e, int b)
{
	return strtol(s, e, b);
}

unsigned long long
strtoull(const char *s, char **e, int b)
{
	return strtoul(s, e, b);
}

/* the C locale: one byte, one character */
int
btowc(int c)
{
	return c;
}

int
wctob(int c)
{
	return c < 256 ? c : -1;
}

int
wcscoll(const wchar_t *a, const wchar_t *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a - *b;
}

char *
strtok_r(char *s, const char *d, char **save)
{
	char *e;

	if (!s)
		s = *save;
	s += strspn(s, d);
	if (!*s) {
		*save = s;
		return 0;
	}
	e = s + strcspn(s, d);
	if (*e)
		*e++ = 0;
	*save = e;
	return s;
}

/* large files do not exist here; no threads to wait for a signal */
long
ftello(void *f)
{
	extern long ftell();

	return ftell(f);
}

int
sigwait(const void *set, int *sig)
{
	return -1;
}

/* no times from the kernel: what asks for them only reports them */
int
getrusage(int who, void *r)
{
	memset(r, 0, 16);
	return 0;
}
