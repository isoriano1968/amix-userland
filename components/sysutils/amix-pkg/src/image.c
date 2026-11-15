/* image.c -- the client's state below the root: var/pkg (IPS-CLIENT-DESIGN.md) */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"

#define CATDIR	"publisher/" PUBLISHER "/catalog"

char *
image_path(const struct image *im, const char *rel)
{
	char *v = root_path(im->root, "var/pkg");
	char *p = root_path(v, rel);

	free(v);
	return p;
}

void
image_open(struct image *im, const char *root)
{
	struct buf b = { 0 };
	char *cfg;

	memset(im, 0, sizeof(*im));
	im->root = xstrdup(root ? root : "/");
	im->devel = 1;
	cfg = image_path(im, "cfg");
	if (read_file(cfg, &b) == 0) {
		char *line = b.data, *nl;

		for (; line && *line; line = nl ? nl + 1 : NULL) {
			nl = strchr(line, '\n');
			if (nl)
				*nl = 0;
			if (!strncmp(line, "origin=", 7))
				im->origin = xstrdup(line + 7);
			else if (!strncmp(line, "devel=", 6))
				im->devel = strcmp(line + 6, "false") != 0;
		}
	}
	buf_free(&b);
	free(cfg);
}

int
image_save_cfg(const struct image *im)
{
	char *dir = image_path(im, ""), *cfg = image_path(im, "cfg");
	char *text = xasprintf("publisher=%s\norigin=%s\ndevel=%s\n", PUBLISHER,
	    im->origin ? im->origin : "", im->devel ? "true" : "false");
	int r = mkdirs(dir, 0755) < 0 ? -1 : write_atomic(cfg, text, strlen(text));

	free(dir);
	free(cfg);
	free(text);
	return r;
}

/* --- catalog signatures ----------------------------------------------------- */

/* SHA-1 of a catalog file without its "_SIGNATURE" member, as pkg5 signs it */
static int
catalog_sig(const struct buf *b, char out[41], char sig[41])
{
	const char *k = strstr(b->data, "\"_SIGNATURE\":");
	const char *from, *to, *s;
	struct buf t = { 0 };

	if (!k)
		return -1;
	s = strstr(k, "\"sha-1\":\"");
	if (!s || strlen(s + 9) < 40)
		return -1;
	memcpy(sig, s + 9, 40);
	sig[40] = 0;
	to = strchr(k, '}');
	if (!to)
		return -1;
	to++;
	from = k;
	if (from > b->data && from[-1] == ',')
		from--;
	else if (*to == ',')
		to++;
	buf_add(&t, b->data, (size_t)(from - b->data));
	buf_add(&t, to, strlen(to));
	sha1_hex(t.data, t.len, out);
	buf_free(&t);
	return 0;
}

static int
fetch_part(const struct image *im, const char *name, const char *want, struct buf *out)
{
	char *rel = xasprintf("%s/%s", CATDIR, name);
	char got[41], sig[41];
	int r = fetch(im->origin, rel, out);

	free(rel);
	if (r < 0) {
		warnx("%s: cannot fetch %s: %s", im->origin, name, strerror(errno));
		return -1;
	}
	if (catalog_sig(out, got, sig) < 0 || strcmp(got, sig) ||
	    (want && strcmp(got, want))) {
		warnx("%s: %s: signature does not match", im->origin, name);
		return -1;
	}
	return 0;
}

static const char *const PARTS[] = { "catalog.base.C", "catalog.dependency.C",
    "catalog.summary.C", NULL };

int
catalog_refresh(struct image *im)
{
	struct buf attrs = { 0 }, part[3] = { { 0 } };
	struct json *j = NULL, *parts;
	char *dir = NULL;
	int i, r = -1;

	if (!im->origin || !*im->origin) {
		warnx("no origin: pkg set-publisher -O URL %s", PUBLISHER);
		return -1;
	}
	if (fetch_part(im, "catalog.attrs", NULL, &attrs) < 0)
		goto out;
	if (!(j = json_parse(attrs.data, attrs.len)) || !(parts = json_get(j, "parts"))) {
		warnx("catalog.attrs: not a catalog");
		goto out;
	}
	for (i = 0; PARTS[i]; i++) {
		struct json *p = json_get(parts, PARTS[i]);
		const char *want = p ? json_str(p, "signature-sha-1") : NULL;

		if (!p) {
			if (i == 0) {
				warnx("catalog.attrs has no %s", PARTS[i]);
				goto out;
			}
			continue;
		}
		if (fetch_part(im, PARTS[i], want, &part[i]) < 0)
			goto out;
	}
	dir = image_path(im, CATDIR);
	if (mkdirs(dir, 0755) < 0) {
		warnx("%s: %s", dir, strerror(errno));
		goto out;
	}
	/* the parts first, catalog.attrs last: a catalog read later is whole */
	for (i = 0; PARTS[i]; i++) {
		char *f;

		if (!part[i].data)
			continue;
		f = root_path(dir, PARTS[i]);
		if (write_atomic(f, part[i].data, part[i].len) < 0) {
			warnx("%s: %s", f, strerror(errno));
			free(f);
			goto out;
		}
		free(f);
	}
	{
		char *f = root_path(dir, "catalog.attrs");

		if (write_atomic(f, attrs.data, attrs.len) < 0) {
			warnx("%s: %s", f, strerror(errno));
			free(f);
			goto out;
		}
		free(f);
	}
	r = 0;
out:
	json_free(j);
	buf_free(&attrs);
	for (i = 0; i < 3; i++)
		buf_free(&part[i]);
	free(dir);
	return r;
}

/* --- the catalog as kept ---------------------------------------------------- */

static struct catpkg *
cat_pkg(struct catalog *c, const char *stem)
{
	size_t i;

	for (i = 0; i < c->n; i++)
		if (!strcmp(c->pkg[i].stem, stem))
			return &c->pkg[i];
	c->pkg = xrealloc(c->pkg, (c->n + 1) * sizeof(*c->pkg));
	memset(&c->pkg[c->n], 0, sizeof(c->pkg[c->n]));
	c->pkg[c->n].stem = xstrdup(stem);
	return &c->pkg[c->n++];
}

static struct catver *
cat_ver(struct catpkg *p, const char *version)
{
	size_t i;

	for (i = 0; i < p->n; i++)
		if (!strcmp(p->v[i].version, version))
			return &p->v[i];
	p->v = xrealloc(p->v, (p->n + 1) * sizeof(*p->v));
	memset(&p->v[p->n], 0, sizeof(p->v[p->n]));
	p->v[p->n].version = xstrdup(version);
	return &p->v[p->n++];
}

static struct json *
load_part(const struct image *im, const char *name)
{
	char *rel = xasprintf("%s/%s", CATDIR, name), *f = image_path(im, rel);
	struct buf b = { 0 };
	struct json *j = NULL;

	if (read_file(f, &b) == 0)
		j = json_parse(b.data, b.len);
	buf_free(&b);
	free(rel);
	free(f);
	return j;
}

static int
cmp_pkg(const void *a, const void *b)
{
	return strcmp(((const struct catpkg *)a)->stem, ((const struct catpkg *)b)->stem);
}

static int
cmp_ver(const void *a, const void *b)
{
	return version_cmp(((const struct catver *)a)->version, ((const struct catver *)b)->version);
}

int
catalog_load(const struct image *im, struct catalog *c)
{
	struct json *base, *pub;
	int part;
	size_t i, k;

	memset(c, 0, sizeof(*c));
	if (!(base = load_part(im, "catalog.base.C")))
		return -1;
	pub = json_get(base, PUBLISHER);
	for (i = 0; pub && i < pub->n; i++) {
		struct catpkg *p = cat_pkg(c, pub->keys[i]);
		struct json *arr = pub->items[i];

		for (k = 0; arr && arr->type == J_ARR && k < arr->n; k++) {
			const char *v = json_str(arr->items[k], "version");
			const char *sig = json_str(arr->items[k], "signature-sha-1");

			if (v)
				cat_ver(p, v)->sig = sig ? xstrdup(sig) : NULL;
		}
	}
	json_free(base);
	/* dependency and summary parts: "actions" per version */
	for (part = 0; part < 2; part++) {
		struct json *j = load_part(im, part ? "catalog.summary.C" : "catalog.dependency.C");

		pub = json_get(j, PUBLISHER);
		for (i = 0; pub && i < pub->n; i++) {
			struct catpkg *p = cat_pkg(c, pub->keys[i]);
			struct json *arr = pub->items[i];

			for (k = 0; arr && arr->type == J_ARR && k < arr->n; k++) {
				const char *v = json_str(arr->items[k], "version");
				struct json *acts = json_get(arr->items[k], "actions");
				struct catver *cv;
				size_t a, nd = 0;

				if (!v || !acts || acts->type != J_ARR)
					continue;
				cv = cat_ver(p, v);
				for (a = 0; a < acts->n; a++) {
					struct action act;

					if (acts->items[a]->type != J_STR ||
					    action_parse(acts->items[a]->str, &act) < 0)
						continue;
					if (!part && !strcmp(act.type, "depend") &&
					    action_get(&act, "fmri") &&
					    (!action_get(&act, "type") ||
					    !strcmp(action_get(&act, "type"), "require"))) {
						cv->depends = xrealloc(cv->depends,
						    (nd + 2) * sizeof(*cv->depends));
						cv->depends[nd++] = xstrdup(action_get(&act, "fmri"));
						cv->depends[nd] = NULL;
					} else if (part && !strcmp(act.type, "set") &&
					    action_get(&act, "name") &&
					    !strcmp(action_get(&act, "name"), "pkg.summary") &&
					    action_get(&act, "value")) {
						free(cv->summary);
						cv->summary = xstrdup(action_get(&act, "value"));
					}
					action_free(&act);
				}
			}
		}
		json_free(j);
	}
	qsort(c->pkg, c->n, sizeof(*c->pkg), cmp_pkg);
	for (i = 0; i < c->n; i++)
		qsort(c->pkg[i].v, c->pkg[i].n, sizeof(*c->pkg[i].v), cmp_ver);
	return 0;
}

void
catalog_free(struct catalog *c)
{
	size_t i, k;

	for (i = 0; i < c->n; i++) {
		for (k = 0; k < c->pkg[i].n; k++) {
			struct catver *v = &c->pkg[i].v[k];
			char **d;

			free(v->version);
			free(v->sig);
			free(v->summary);
			for (d = v->depends; d && *d; d++)
				free(*d);
			free(v->depends);
		}
		free(c->pkg[i].v);
		free(c->pkg[i].stem);
	}
	free(c->pkg);
	memset(c, 0, sizeof(*c));
}

const struct catpkg *
catalog_find(const struct catalog *c, const char *stem)
{
	size_t lo = 0, hi = c->n;

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strcmp(c->pkg[mid].stem, stem);

		if (!r)
			return &c->pkg[mid];
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

/* --- installed packages ----------------------------------------------------- */

static int
cmp_inst(const void *a, const void *b)
{
	return strcmp(((const struct installed *)a)->stem, ((const struct installed *)b)->stem);
}

int
installed_list(const struct image *im, struct installed **out, size_t *n)
{
	char *dir = image_path(im, "installed");
	DIR *d = opendir(dir);
	struct dirent *e;

	*out = NULL;
	*n = 0;
	if (!d) {
		free(dir);
		return 0;		/* nothing installed yet */
	}
	while ((e = readdir(d))) {
		struct buf b = { 0 };
		char *f;
		struct installed in;
		struct fmri fm;
		const char *fv;

		if (e->d_name[0] == '.' || strstr(e->d_name, ".new"))
			continue;
		f = root_path(dir, e->d_name);
		if (read_file(f, &b) < 0 || manifest_parse(b.data, b.len, &in.m) < 0) {
			warnx("%s: unreadable", f);
			buf_free(&b);
			free(f);
			continue;
		}
		buf_free(&b);
		free(f);
		fv = manifest_set(&in.m, "pkg.fmri");
		if (!fv || fmri_parse(fv, &fm) < 0) {
			warnx("installed/%s: no pkg.fmri", e->d_name);
			manifest_free(&in.m);
			continue;
		}
		in.stem = fm.stem;
		in.version = fm.version ? fm.version : xstrdup("0");
		free(fm.publisher);
		*out = xrealloc(*out, (*n + 1) * sizeof(**out));
		(*out)[(*n)++] = in;
	}
	closedir(d);
	free(dir);
	qsort(*out, *n, sizeof(**out), cmp_inst);
	return 0;
}

void
installed_free(struct installed *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		free(p[i].stem);
		free(p[i].version);
		manifest_free(&p[i].m);
	}
	free(p);
}

int
remote_manifest(const struct image *im, const char *stem, const char *version,
    const char *sig, struct buf *out)
{
	char *qs = url_quote(stem), *qv = url_quote(version);
	char *rel = xasprintf("publisher/%s/pkg/%s/%s", PUBLISHER, qs, qv);
	char got[41];
	int r = fetch(im->origin, rel, out);

	free(qs);
	free(qv);
	free(rel);
	if (r < 0) {
		warnx("%s@%s: cannot fetch the manifest: %s", stem, version, strerror(errno));
		return -1;
	}
	sha1_hex(out->data, out->len, got);
	if (sig && strcmp(got, sig)) {
		warnx("%s@%s: the manifest does not match the catalog", stem, version);
		return -1;
	}
	return 0;
}
