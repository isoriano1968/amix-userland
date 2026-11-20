/* action.c -- IPS manifest lines: type [payload] key=value key="quoted value" ... */
#include <stdlib.h>
#include <string.h>

#include "pkg.h"

static int
is_space(char c)
{
	return c == ' ' || c == '\t';
}

/* a token: plain up to a space, or "..."/'...' with \ escapes.  Two passes,
 * the size, then the copy, into one exact allocation: a manifest holds
 * thousands of these, and a byte-at-a-time buffer cost a 68030 most of
 * pkg list's time (2026-11-20: 33 s for 60 installed packages). */
static char *
token(const char **sp)
{
	const char *s = *sp, *p;
	size_t n = 0;
	char *out, *o;

	for (p = s; *p && !is_space(*p);) {
		if (*p == '"' || *p == '\'') {
			char q = *p++;

			while (*p && *p != q) {
				if (*p == '\\' && p[1])
					p++;
				p++;
				n++;
			}
			if (*p != q)
				return NULL;
			p++;
		} else {
			p++;
			n++;
		}
	}
	o = out = xmalloc(n + 1);
	for (p = s; *p && !is_space(*p);) {
		if (*p == '"' || *p == '\'') {
			char q = *p++;

			while (*p != q) {
				if (*p == '\\' && p[1])
					p++;
				*o++ = *p++;
			}
			p++;
		} else
			*o++ = *p++;
	}
	*o = 0;
	*sp = p;
	return out;
}

int
action_parse(const char *line, struct action *a)
{
	const char *s = line;
	char *t;

	memset(a, 0, sizeof(*a));
	while (is_space(*s))
		s++;
	if (!*s || *s == '#')
		return -1;
	{
		const char *e = s;

		while (*e && !is_space(*e))
			e++;
		a->type = xmalloc((size_t)(e - s) + 1);
		memcpy(a->type, s, (size_t)(e - s));
		a->type[e - s] = 0;
		s = e;
	}
	for (;;) {
		char *eq;

		while (is_space(*s))
			s++;
		if (!*s || *s == '\n' || *s == '\r')
			break;
		/* the payload: a first word without '=' (file, license) */
		if (!a->nattr && !a->payload) {
			const char *e = s;

			while (*e && !is_space(*e) && *e != '=' && *e != '"')
				e++;
			if (!*e || is_space(*e)) {
				a->payload = xmalloc((size_t)(e - s) + 1);
				memcpy(a->payload, s, (size_t)(e - s));
				a->payload[e - s] = 0;
				s = e;
				continue;
			}
		}
		if (!(t = token(&s)))
			goto bad;
		if (!(eq = strchr(t, '='))) {
			free(t);
			goto bad;
		}
		*eq = 0;
		a->attr = xrealloc(a->attr, (a->nattr + 1) * sizeof(*a->attr));
		a->attr[a->nattr].key = t;		/* key and value share one block */
		a->attr[a->nattr].value = eq + 1;
		a->nattr++;
	}
	return 0;
bad:
	action_free(a);
	return -1;
}

void
action_free(struct action *a)
{
	size_t i;

	for (i = 0; i < a->nattr; i++)
		free(a->attr[i].key);
	free(a->attr);
	free(a->type);
	free(a->payload);
	memset(a, 0, sizeof(*a));
}

const char *
action_get_n(const struct action *a, const char *key, int n)
{
	size_t i;

	for (i = 0; i < a->nattr; i++)
		if (!strcmp(a->attr[i].key, key) && n-- == 0)
			return a->attr[i].value;
	return NULL;
}

const char *
action_get(const struct action *a, const char *key)
{
	return action_get_n(a, key, 0);
}

int
manifest_parse(const char *text, size_t len, struct manifest *m)
{
	const char *s = text, *end = text + len;
	char *line = NULL;

	memset(m, 0, sizeof(*m));
	while (s < end) {
		const char *e = memchr(s, '\n', (size_t)(end - s));
		struct action a;

		if (!e)
			e = end;
		line = xrealloc(line, (size_t)(e - s) + 1);
		memcpy(line, s, (size_t)(e - s));
		line[e - s] = 0;
		s = e + 1;
		if (action_parse(line, &a) < 0) {
			const char *c = line;

			while (is_space(*c))
				c++;
			if (*c && *c != '#' && *c != '\r') {
				free(line);
				manifest_free(m);
				return -1;
			}
			continue;
		}
		m->act = xrealloc(m->act, (m->n + 1) * sizeof(*m->act));
		m->act[m->n++] = a;
	}
	free(line);
	return 0;
}

void
manifest_free(struct manifest *m)
{
	size_t i;

	for (i = 0; i < m->n; i++)
		action_free(&m->act[i]);
	free(m->act);
	memset(m, 0, sizeof(*m));
}

const char *
manifest_set(const struct manifest *m, const char *name)
{
	size_t i;

	for (i = 0; i < m->n; i++) {
		const struct action *a = &m->act[i];
		const char *nm;

		if (!strcmp(a->type, "set") && (nm = action_get(a, "name")) && !strcmp(nm, name))
			return action_get(a, "value");
	}
	return NULL;
}

/* pkg.content-hash=file:sha512t_256:<hex>: the hex for "file" or "gzip" */
const char *
content_hash(const struct action *a, const char *kind)
{
	const char *v;
	size_t kl = strlen(kind);
	int n;

	for (n = 0; (v = action_get_n(a, "pkg.content-hash", n)); n++)
		if (!strncmp(v, kind, kl) && !strncmp(v + kl, ":sha512t_256:", 13))
			return v + kl + 13;
	return NULL;
}
