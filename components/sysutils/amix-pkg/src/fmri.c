/* fmri.c -- pkg://publisher/category/name@release,build-branch:timestamp */
#include <stdlib.h>
#include <string.h>

#include "pkg.h"

int
fmri_parse(const char *s, struct fmri *f)
{
	const char *at;

	memset(f, 0, sizeof(*f));
	if (!strncmp(s, "pkg://", 6)) {
		const char *slash = strchr(s + 6, '/');

		if (!slash)
			return -1;
		f->publisher = xmalloc((size_t)(slash - s - 6) + 1);
		memcpy(f->publisher, s + 6, (size_t)(slash - s - 6));
		f->publisher[slash - s - 6] = 0;
		s = slash + 1;
	} else if (!strncmp(s, "pkg:/", 5))
		s += 5;
	while (*s == '/')
		s++;
	at = strchr(s, '@');
	f->stem = xmalloc((size_t)(at ? at - s : (long)strlen(s)) + 1);
	memcpy(f->stem, s, at ? (size_t)(at - s) : strlen(s));
	f->stem[at ? at - s : (long)strlen(s)] = 0;
	if (at && at[1])
		f->version = xstrdup(at + 1);
	if (!*f->stem) {
		fmri_free(f);
		return -1;
	}
	return 0;
}

void
fmri_free(struct fmri *f)
{
	free(f->publisher);
	free(f->stem);
	free(f->version);
	memset(f, 0, sizeof(*f));
}

/* compare dotted integers "1.10.2" "1.9"; missing parts count as 0 */
static int
dotted_cmp(const char *a, size_t al, const char *b, size_t bl)
{
	const char *ae = a + al, *be = b + bl;

	while (a < ae || b < be) {
		unsigned long x = 0, y = 0;

		while (a < ae && *a != '.')
			x = x * 10 + (unsigned long)(*a++ - '0');
		while (b < be && *b != '.')
			y = y * 10 + (unsigned long)(*b++ - '0');
		if (x != y)
			return x < y ? -1 : 1;
		if (a < ae)
			a++;
		if (b < be)
			b++;
	}
	return 0;
}

/* split "rel,build-branch:ts" into pointers and lengths */
struct vparts {
	const char *rel, *branch, *ts;
	size_t rl, bl, tl;
};

static void
vsplit(const char *v, struct vparts *p)
{
	const char *comma = strchr(v, ','), *dash, *colon = strchr(v, ':');
	const char *end = colon ? colon : v + strlen(v);

	memset(p, 0, sizeof(*p));
	p->rel = v;
	p->rl = (size_t)((comma && comma < end ? comma : end) - v);
	dash = strchr(v, '-');
	if (dash && dash < end) {
		p->branch = dash + 1;
		p->bl = (size_t)(end - dash - 1);
	}
	if (colon) {
		p->ts = colon + 1;
		p->tl = strlen(colon + 1);
	}
}

int
version_cmp(const char *a, const char *b)
{
	struct vparts x, y;
	int c;

	vsplit(a, &x);
	vsplit(b, &y);
	if ((c = dotted_cmp(x.rel, x.rl, y.rel, y.rl)))
		return c;
	if ((c = dotted_cmp(x.branch ? x.branch : "", x.bl, y.branch ? y.branch : "", y.bl)))
		return c;
	if (x.tl && y.tl) {
		size_t n = x.tl < y.tl ? x.tl : y.tl;

		if ((c = strncmp(x.ts, y.ts, n)))
			return c < 0 ? -1 : 1;
	}
	return 0;
}

char *
version_short(const char *v)
{
	const char *colon = strchr(v, ':');
	char *s;

	if (!colon)
		return xstrdup(v);
	s = xmalloc((size_t)(colon - v) + 1);
	memcpy(s, v, (size_t)(colon - v));
	s[colon - v] = 0;
	return s;
}

int
version_at_least(const char *v, const char *min)
{
	struct vparts x, y;

	vsplit(v, &x);
	vsplit(min, &y);
	return dotted_cmp(x.rel, x.rl, y.rel, y.rl) >= 0;
}
