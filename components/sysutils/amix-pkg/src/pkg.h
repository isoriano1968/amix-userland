/*
 * pkg -- the AMIX IPS client (doc/plans/IPS-CLIENT-DESIGN.md).
 * Portable C99: AMIX (gcc 14 cross, libamixc99, libcurl, zlib) and the
 * host, for tests.
 */
#ifndef AMIX_PKG_H
#define AMIX_PKG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PUBLISHER	"amix"

/* --- util.c -------------------------------------------------------------- */
extern const char *progname;
extern int verbose;

void die(const char *fmt, ...);
void warnx(const char *fmt, ...);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...);

struct buf {
	char *data;
	size_t len, cap;
};
void buf_add(struct buf *b, const void *p, size_t n);
void buf_str(struct buf *b, const char *s);
void buf_free(struct buf *b);

int read_file(const char *path, struct buf *out);	/* 0 or -1 (errno) */
int write_atomic(const char *path, const void *p, size_t n);	/* .new, fsync, rename */
int mkdirs(const char *path, int mode);
char *root_path(const char *root, const char *path);	/* root + path */
char *url_quote(const char *s);			/* "/" "," ":" -> %2F %2C %3A */
char *url_unquote(const char *s);
unsigned long long parse_ull(const char *s);		/* decimal; AMIX has no strtoull */

/* --- hash.c -------------------------------------------------------------- */
void sha1_hex(const void *p, size_t n, char out[41]);
void sha512_256_hex(const void *p, size_t n, char out[65]);

/* --- json.c -------------------------------------------------------------- */
enum jtype { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
struct json {
	enum jtype type;
	char *str;		/* J_STR, J_NUM (as text) */
	int boolean;
	size_t n;		/* J_ARR, J_OBJ: number of members */
	char **keys;		/* J_OBJ */
	struct json **items;	/* J_ARR, J_OBJ */
};
struct json *json_parse(const char *text, size_t len);	/* NULL on error */
void json_free(struct json *j);
struct json *json_get(const struct json *obj, const char *key);
const char *json_str(const struct json *obj, const char *key);

/* --- action.c ------------------------------------------------------------ */
struct attr {
	char *key, *value;
};
struct action {
	char *type;		/* set, file, dir, link, hardlink, depend, license */
	char *payload;		/* file/license: the SHA-1 hash */
	size_t nattr;
	struct attr *attr;
};
int action_parse(const char *line, struct action *a);	/* 0 or -1 */
void action_free(struct action *a);
const char *action_get(const struct action *a, const char *key);
/* the n-th value of a repeated key (pkg.content-hash), or NULL */
const char *action_get_n(const struct action *a, const char *key, int n);

struct manifest {
	size_t n;
	struct action *act;
};
int manifest_parse(const char *text, size_t len, struct manifest *m);
void manifest_free(struct manifest *m);
const char *manifest_set(const struct manifest *m, const char *name);
const char *content_hash(const struct action *a, const char *kind);	/* "file" / "gzip" */

/* --- fmri.c -------------------------------------------------------------- */
struct fmri {
	char *publisher;	/* may be NULL */
	char *stem;		/* category/name */
	char *version;		/* 1.1,2.1-2:20261115T112620Z (may be NULL) */
};
int fmri_parse(const char *s, struct fmri *f);
void fmri_free(struct fmri *f);
/* <0, 0, >0 by release, then branch, then timestamp */
int version_cmp(const char *a, const char *b);
/* the version without the timestamp, for display */
char *version_short(const char *v);
/* does version v satisfy the minimum "1" or "1.2" (release prefix compare) */
int version_at_least(const char *v, const char *min);

/* --- fetch.c ------------------------------------------------------------- */
/* origin: file:///path, /path, or http(s)://host/path (libcurl builds) */
int fetch(const char *origin, const char *relpath, struct buf *out);
int gunzip(const struct buf *in, struct buf *out);

/* --- image.c: the client's state under the root -------------------------- */
struct image {
	char *root;		/* "/" or -R */
	char *origin;		/* from var/pkg/cfg */
	int devel;		/* facet.devel */
};
void image_open(struct image *im, const char *root);
char *image_path(const struct image *im, const char *rel);	/* root/var/pkg/rel */
int image_save_cfg(const struct image *im);

/* the catalog: stem -> versions with summary */
struct catver {
	char *version;
	char *sig;		/* manifest SHA-1 */
	char *summary;
	char **depends;		/* "library/world@1", NULL-terminated */
};
struct catpkg {
	char *stem;
	size_t n;
	struct catver *v;	/* sorted oldest first */
};
struct catalog {
	size_t n;
	struct catpkg *pkg;	/* sorted by stem */
};
int catalog_load(const struct image *im, struct catalog *c);	/* 0, -1 none */
void catalog_free(struct catalog *c);
const struct catpkg *catalog_find(const struct catalog *c, const char *stem);
int catalog_refresh(struct image *im);

/* installed packages: var/pkg/installed/<stem> */
struct installed {
	char *stem;
	char *version;
	struct manifest m;
};
int installed_list(const struct image *im, struct installed **out, size_t *n);
void installed_free(struct installed *p, size_t n);

/* a manifest from the repository, checked against the catalog's SHA-1 */
int remote_manifest(const struct image *im, const char *stem, const char *version,
    const char *sig, struct buf *out);

#endif
