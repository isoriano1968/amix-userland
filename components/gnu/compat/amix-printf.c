/* amix-printf.c -- printf for GNU tools: AMIX's has no %j, %z or %t and no long
   long.  libamixc99's vsnprintf does the work; %j is 64 bits here, so it becomes
   %ll on the way.  The names are in amix-gnu.h. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#undef vsnprintf
extern int vsnprintf(char *, size_t, const char *, va_list);

static const char *
fix(const char *f, char *b, size_t n)
{
	size_t i = 0;
	int in = 0;

	if (!strchr(f, 'j') || strlen(f) * 2 >= n)
		return f;
	for (; *f; f++) {
		if (!in) {
			if (*f == '%')
				in = 1;
			b[i++] = *f;
			continue;
		}
		if (*f == 'j') {
			b[i++] = 'l';
			b[i++] = 'l';
			continue;
		}
		b[i++] = *f;
		if (*f == '%' || strchr("diouxXeEfFgGaAcspn", *f))
			in = 0;
	}
	b[i] = 0;
	return b;
}

int
gnu_vsnprintf(char *s, size_t n, const char *f, va_list ap)
{
	char b[1024];

	return vsnprintf(s, n, fix(f, b, sizeof b), ap);
}

int
gnu_vfprintf(FILE *o, const char *f, va_list ap)
{
	char b[8192], *p = b;
	va_list c;
	int n;

	va_copy(c, ap);
	n = gnu_vsnprintf(b, sizeof b, f, ap);
	if (n >= (int)sizeof b && (p = malloc((size_t)n + 1)) != 0)
		gnu_vsnprintf(p, (size_t)n + 1, f, c);
	else if (n >= (int)sizeof b)
		p = b, n = sizeof b - 1;
	va_end(c);
	if (n > 0)
		fwrite(p, 1, (size_t)n, o);
	if (p != b)
		free(p);
	return n;
}

int
gnu_vprintf(const char *f, va_list ap)
{
	return gnu_vfprintf(stdout, f, ap);
}

int
gnu_vsprintf(char *s, const char *f, va_list ap)
{
	return gnu_vsnprintf(s, 1 << 30, f, ap);
}

#define WRAP(call) { va_list ap; int r; va_start(ap, f); r = call; va_end(ap); return r; }
int gnu_printf(const char *f, ...) WRAP(gnu_vfprintf(stdout, f, ap))
int gnu_fprintf(FILE *o, const char *f, ...) WRAP(gnu_vfprintf(o, f, ap))
int gnu_sprintf(char *s, const char *f, ...) WRAP(gnu_vsprintf(s, f, ap))
int gnu_snprintf(char *s, size_t n, const char *f, ...) WRAP(gnu_vsnprintf(s, n, f, ap))
