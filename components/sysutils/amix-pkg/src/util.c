/* util.c -- errors, memory, buffers, files written safely */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "pkg.h"

const char *progname = "pkg";
int verbose;

void
die(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void *
xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		die("out of memory");
	return p;
}

void *
xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if (!p)
		die("out of memory");
	return p;
}

char *
xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;

	return memcpy(xmalloc(n), s, n);
}

/* grows the buffer: older vsnprintf()s return -1 on truncation, not the size */
char *
xasprintf(const char *fmt, ...)
{
	va_list ap;
	size_t sz = 128;
	char *s = NULL;

	for (;;) {
		int n;

		s = xrealloc(s, sz);
		va_start(ap, fmt);
		n = vsnprintf(s, sz, fmt, ap);
		va_end(ap);
		if (n >= 0 && (size_t)n < sz)
			return s;
		sz = n >= 0 ? (size_t)n + 1 : sz * 2;
	}
}

void
buf_add(struct buf *b, const void *p, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->data = xrealloc(b->data, b->cap);
	}
	memcpy(b->data + b->len, p, n);
	b->len += n;
	b->data[b->len] = 0;		/* always a C string as well */
}

void
buf_str(struct buf *b, const char *s)
{
	buf_add(b, s, strlen(s));
}

void
buf_free(struct buf *b)
{
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

int
read_file(const char *path, struct buf *out)
{
	char tmp[8192];
	ssize_t n;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return -1;
	buf_add(out, "", 0);
	while ((n = read(fd, tmp, sizeof(tmp))) > 0)
		buf_add(out, tmp, (size_t)n);
	if (n < 0) {
		int e = errno;

		close(fd);
		errno = e;
		return -1;
	}
	close(fd);
	return 0;
}

/* path.new, written in full and synced, then renamed over path */
int
write_atomic(const char *path, const void *p, size_t n)
{
	char *tmp = xasprintf("%s.new", path);
	const char *c = p;
	int fd, e;

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		goto fail;
	while (n) {
		ssize_t w = write(fd, c, n);

		if (w <= 0) {
			e = errno;
			close(fd);
			unlink(tmp);
			errno = e;
			goto fail;
		}
		c += w;
		n -= (size_t)w;
	}
	if (fsync(fd) < 0 || close(fd) < 0 || rename(tmp, path) < 0) {
		e = errno;
		unlink(tmp);
		errno = e;
		goto fail;
	}
	free(tmp);
	return 0;
fail:
	free(tmp);
	return -1;
}

int
mkdirs(const char *path, int mode)
{
	char *p = xstrdup(path), *s;
	struct stat st;

	for (s = p + 1; ; s++) {
		if (*s == '/' || *s == 0) {
			char c = *s;

			*s = 0;
			if (stat(p, &st) < 0 && mkdir(p, (mode_t)mode) < 0 && errno != EEXIST) {
				free(p);
				return -1;
			}
			*s = c;
			if (!c)
				break;
		}
	}
	free(p);
	return 0;
}

char *
root_path(const char *root, const char *path)
{
	size_t rl = strlen(root);

	while (*path == '/')
		path++;
	if (rl && root[rl - 1] == '/')
		return xasprintf("%s%s", root, path);
	return xasprintf("%s/%s", root, path);
}

char *
url_quote(const char *s)
{
	struct buf b = { 0 };

	buf_add(&b, "", 0);
	for (; *s; s++) {
		if (*s == '/')
			buf_str(&b, "%2F");
		else if (*s == ',')
			buf_str(&b, "%2C");
		else if (*s == ':')
			buf_str(&b, "%3A");
		else if (*s == '%')
			buf_str(&b, "%25");
		else
			buf_add(&b, s, 1);
	}
	return b.data;
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

char *
url_unquote(const char *s)
{
	char *out = xmalloc(strlen(s) + 1), *o = out;

	while (*s) {
		if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
			*o++ = (char)(hexval(s[1]) * 16 + hexval(s[2]));
			s += 3;
		} else
			*o++ = *s++;
	}
	*o = 0;
	return out;
}

unsigned long long
parse_ull(const char *s)
{
	unsigned long long n = 0;

	while (*s >= '0' && *s <= '9')
		n = n * 10 + (unsigned long long)(*s++ - '0');
	return n;
}

/* a symlink to a directory (lst from lstat): a directory moved to another
 * disk, such as /usr/local -> /amixdata/usr-local, still counts as one */
int
link_to_dir(const char *path, const struct stat *lst)
{
	struct stat st;

	return S_ISLNK(lst->st_mode) && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/*
 * A 68060 here?  The port kernel names the CPU in uname's machine string
 * ("Amiga (Unlimited) 68060-..."; amix-gate port/src/inituname040.s); stock
 * AMIX names none, and runs on the A3000's 68030.  PKG_CPU=060 or 030
 * decides instead (tests).
 */
int
cpu_has_060(void)
{
	const char *e = getenv("PKG_CPU");
	struct utsname u;

	if (e && *e)
		return strstr(e, "60") != NULL;
	if (uname(&u) < 0)
		return 0;
	return strstr(u.machine, "68060") != NULL;
}

/* shell-style pattern: *, ? and [set] (SVR4.0 has no fnmatch) */
int
glob_match(const char *p, const char *s)
{
	for (; *p; p++, s++) {
		if (*p == '*') {
			while (p[1] == '*')
				p++;
			if (!p[1])
				return 1;
			for (; *s; s++)
				if (glob_match(p + 1, s))
					return 1;
			return 0;
		}
		if (!*s)
			return 0;
		if (*p == '?')
			continue;
		if (*p == '[') {
			const char *q = p + 1;
			int neg = (*q == '!' || *q == '^'), hit = 0;

			if (neg)
				q++;
			for (; *q && *q != ']'; q++)
				if (q[1] == '-' && q[2] && q[2] != ']') {
					hit |= *s >= q[0] && *s <= q[2];
					q += 2;
				} else
					hit |= *s == *q;
			if (!*q || hit == neg)
				return 0;
			p = q;
			continue;
		}
		if (*p != *s)
			return 0;
	}
	return !*s;
}

int
has_glob(const char *p)
{
	return strpbrk(p, "*?[") != NULL;
}

/* IPS's rule: the pattern matches the whole stem, or a part of it that
 * starts after a "/" ("hello" and "h*" for demo/hello, "gnu/" and a star for gnu/sed) */
int
stem_match(const char *pat, const char *stem)
{
	const char *s;

	if (!strncmp(pat, "pkg:/", 5))
		pat += 5;
	for (s = stem; s; s = strchr(s, '/') ? strchr(s, '/') + 1 : NULL)
		if (has_glob(pat) ? glob_match(pat, s) : !strcmp(pat, s))
			return 1;
	return 0;
}
