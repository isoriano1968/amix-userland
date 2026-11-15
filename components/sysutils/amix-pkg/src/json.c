/* json.c -- a small JSON reader for the IPS catalog (catalog v1) */
#include <stdlib.h>
#include <string.h>

#include "pkg.h"

struct jp {
	const char *s, *end;
	int depth;
};

static void
ws(struct jp *p)
{
	while (p->s < p->end && (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r'))
		p->s++;
}

static struct json *
node(enum jtype t)
{
	struct json *j = xmalloc(sizeof(*j));

	memset(j, 0, sizeof(*j));
	j->type = t;
	return j;
}

static void
put_utf8(struct buf *b, unsigned c)
{
	char u[4];

	if (c < 0x80) {
		u[0] = (char)c;
		buf_add(b, u, 1);
	} else if (c < 0x800) {
		u[0] = (char)(0xc0 | c >> 6);
		u[1] = (char)(0x80 | (c & 0x3f));
		buf_add(b, u, 2);
	} else if (c < 0x10000) {
		u[0] = (char)(0xe0 | c >> 12);
		u[1] = (char)(0x80 | (c >> 6 & 0x3f));
		u[2] = (char)(0x80 | (c & 0x3f));
		buf_add(b, u, 3);
	} else {
		u[0] = (char)(0xf0 | c >> 18);
		u[1] = (char)(0x80 | (c >> 12 & 0x3f));
		u[2] = (char)(0x80 | (c >> 6 & 0x3f));
		u[3] = (char)(0x80 | (c & 0x3f));
		buf_add(b, u, 4);
	}
}

static int
hex4(const char *s, unsigned *v)
{
	int i;

	*v = 0;
	for (i = 0; i < 4; i++) {
		char c = s[i];

		*v <<= 4;
		if (c >= '0' && c <= '9')
			*v |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f')
			*v |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			*v |= (unsigned)(c - 'A' + 10);
		else
			return -1;
	}
	return 0;
}

static char *
string(struct jp *p)
{
	struct buf b = { 0 };

	if (p->s >= p->end || *p->s != '"')
		return NULL;
	p->s++;
	buf_add(&b, "", 0);
	while (p->s < p->end && *p->s != '"') {
		if (*p->s == '\\') {
			unsigned c;

			if (++p->s >= p->end)
				goto bad;
			switch (*p->s) {
			case '"': case '\\': case '/':
				buf_add(&b, p->s, 1); break;
			case 'b': buf_add(&b, "\b", 1); break;
			case 'f': buf_add(&b, "\f", 1); break;
			case 'n': buf_add(&b, "\n", 1); break;
			case 'r': buf_add(&b, "\r", 1); break;
			case 't': buf_add(&b, "\t", 1); break;
			case 'u':
				if (p->end - p->s < 5 || hex4(p->s + 1, &c))
					goto bad;
				p->s += 4;
				if (c >= 0xd800 && c < 0xdc00 && p->end - p->s >= 7 &&
				    p->s[1] == '\\' && p->s[2] == 'u') {
					unsigned lo;

					if (!hex4(p->s + 3, &lo) && lo >= 0xdc00 && lo < 0xe000) {
						c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
						p->s += 6;
					}
				}
				put_utf8(&b, c);
				break;
			default:
				goto bad;
			}
			p->s++;
		} else
			buf_add(&b, p->s++, 1);
	}
	if (p->s >= p->end)
		goto bad;
	p->s++;
	return b.data;
bad:
	buf_free(&b);
	return NULL;
}

static struct json *value(struct jp *p);

static void
member_add(struct json *j, char *key, struct json *v)
{
	j->items = xrealloc(j->items, (j->n + 1) * sizeof(*j->items));
	j->items[j->n] = v;
	if (j->type == J_OBJ) {
		j->keys = xrealloc(j->keys, (j->n + 1) * sizeof(*j->keys));
		j->keys[j->n] = key;
	}
	j->n++;
}

static struct json *
container(struct jp *p, enum jtype t)
{
	struct json *j = node(t);
	char close = t == J_OBJ ? '}' : ']';

	p->s++;
	ws(p);
	if (p->s < p->end && *p->s == close) {
		p->s++;
		return j;
	}
	for (;;) {
		char *key = NULL;
		struct json *v;

		ws(p);
		if (t == J_OBJ) {
			if (!(key = string(p)))
				goto bad;
			ws(p);
			if (p->s >= p->end || *p->s != ':') {
				free(key);
				goto bad;
			}
			p->s++;
		}
		if (!(v = value(p))) {
			free(key);
			goto bad;
		}
		member_add(j, key, v);
		ws(p);
		if (p->s < p->end && *p->s == ',') {
			p->s++;
			continue;
		}
		if (p->s < p->end && *p->s == close) {
			p->s++;
			return j;
		}
		goto bad;
	}
bad:
	json_free(j);
	return NULL;
}

static struct json *
value(struct jp *p)
{
	struct json *j;

	ws(p);
	if (p->s >= p->end || ++p->depth > 64)
		return NULL;
	switch (*p->s) {
	case '{':
		j = container(p, J_OBJ);
		break;
	case '[':
		j = container(p, J_ARR);
		break;
	case '"': {
		char *s = string(p);

		if (!s)
			return NULL;
		j = node(J_STR);
		j->str = s;
		break;
	}
	case 't':
	case 'f':
	case 'n': {
		static const char *w[] = { "true", "false", "null" };
		int i;

		for (i = 0; i < 3; i++) {
			size_t l = strlen(w[i]);

			if ((size_t)(p->end - p->s) >= l && !memcmp(p->s, w[i], l)) {
				p->s += l;
				j = node(i == 2 ? J_NULL : J_BOOL);
				j->boolean = i == 0;
				goto done;
			}
		}
		return NULL;
	}
	default: {
		const char *s = p->s;

		while (p->s < p->end && strchr("+-0123456789.eE", *p->s))
			p->s++;
		if (p->s == s)
			return NULL;
		j = node(J_NUM);
		j->str = xmalloc((size_t)(p->s - s) + 1);
		memcpy(j->str, s, (size_t)(p->s - s));
		j->str[p->s - s] = 0;
	}
	}
done:
	p->depth--;
	return j;
}

struct json *
json_parse(const char *text, size_t len)
{
	struct jp p = { text, text + len, 0 };
	struct json *j = value(&p);

	if (!j)
		return NULL;
	ws(&p);
	if (p.s != p.end) {
		json_free(j);
		return NULL;
	}
	return j;
}

void
json_free(struct json *j)
{
	size_t i;

	if (!j)
		return;
	for (i = 0; i < j->n; i++) {
		json_free(j->items[i]);
		if (j->keys)
			free(j->keys[i]);
	}
	free(j->items);
	free(j->keys);
	free(j->str);
	free(j);
}

struct json *
json_get(const struct json *obj, const char *key)
{
	size_t i;

	if (!obj || obj->type != J_OBJ)
		return NULL;
	for (i = 0; i < obj->n; i++)
		if (!strcmp(obj->keys[i], key))
			return obj->items[i];
	return NULL;
}

const char *
json_str(const struct json *obj, const char *key)
{
	struct json *v = json_get(obj, key);

	return v && v->type == J_STR ? v->str : NULL;
}
