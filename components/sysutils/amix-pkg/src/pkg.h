/*
 * pkg -- the AMIX IPS client (doc/plans/IPS-CLIENT-DESIGN.md).
 * Portable C99: AMIX (gcc 14 cross, libamixc99, zlib; own HTTP, no TLS) and the
 * host, for tests.
 */
#ifndef AMIX_PKG_H
#define AMIX_PKG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef S_ISLNK			/* AMIX's sys/stat.h has S_IFLNK only */
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif

#define PUBLISHER	"amix"

/* --- util.c -------------------------------------------------------------- */
extern const char *progname;
extern char **self_argv;	/* main's argv: pkg runs itself again after its own update */
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
int link_to_dir(const char *path, const struct stat *lst);
int write_atomic(const char *path, const void *p, size_t n);	/* .new, fsync, rename */
int mkdirs(const char *path, int mode);
char *root_path(const char *root, const char *path);	/* root + path */
char *url_quote(const char *s);			/* "/" "," ":" -> %2F %2C %3A */
char *url_unquote(const char *s);
unsigned long long parse_ull(const char *s);		/* decimal; AMIX has no strtoull */

/* --- hash.c -------------------------------------------------------------- */
void sha1_hex(const void *p, size_t n, char out[41]);
void sha512_256_hex(const void *p, size_t n, char out[65]);
enum { HASH_SHA1, HASH_SHA512_256 };
struct hash {			/* either, in pieces */
	int kind;
	uint32_t h1[5];
	uint64_t h5[8];
	uint8_t blk[128];
	size_t fill;
	uint64_t total;
};
void hash_init(struct hash *x, int kind);
void hash_update(struct hash *x, const void *p, size_t n);
void hash_final(struct hash *x, char *out);		/* 41 or 65 bytes */
int hash_file(const char *path, int kind, char *out);	/* 0, or -1 (errno) */

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
/* origin: file:///path, /path, or http://host[:port]/path */
/* where streamed bytes go: 0, or -1 to stop */
typedef int (*http_sink)(const void *p, size_t n, void *arg);	/* 0, or -1 to stop */
int fetch(const char *origin, const char *relpath, struct buf *out);
int fetch_stream(const char *origin, const char *relpath, http_sink sink, void *arg);

/* --- http.c -------------------------------------------------------------- */
int http_split(const char *url, char *host, size_t hlen, int *port, const char **path);
/* the HTTP status (redirects followed), or -1 with errno; the body into out */
int http_request(const char *method, const char *url, const void *body, size_t blen,
    struct buf *out);
int http_stream(const char *method, const char *url, const void *body, size_t blen,
    http_sink sink, void *arg);
int gunzip(const struct buf *in, struct buf *out);

/* --- image.c: the client's state under the root -------------------------- */
struct image {
	char *root;		/* "/" or -R */
	char *origin;		/* the origin in use: active=, else the first one on */
	char **origins;		/* every origin of the publisher, in order (var/pkg/cfg) */
	int *off;		/* off[i]: origins[i] is kept but not used */
	size_t norigins;
	int devel;		/* facet.devel */
};
void image_open(struct image *im, const char *root);
char *image_path(const struct image *im, const char *rel);	/* root/var/pkg/rel */
int image_save_cfg(const struct image *im);
/* origins: add (at the end), remove ("*": all), switch on or off; 0, or -1 unknown */
void image_add_origin(struct image *im, const char *url);
int image_remove_origin(struct image *im, const char *url);
int image_origin_off(struct image *im, const char *url, int off);
/* does this machine have a 68060 (the kernel's uname tag; PKG_CPU=060|030 for tests) */
int cpu_has_060(void);
int glob_match(const char *pat, const char *s);
int has_glob(const char *p);
int stem_match(const char *pat, const char *stem);
/* hw.c: the boards (org.amix.hw); PKG_HW overrides */
const char *hw_list(void);
int hw_has(const char *want);
extern const char *hw_why;

/* the catalog: stem -> versions with summary */
struct catver {
	char *version;
	char *sig;		/* manifest SHA-1 */
	char *summary;
	char *cpu;		/* org.amix.cpu: "68020", "68060" or NULL (untagged) */
	char *hw;		/* org.amix.hw: "z3660-core1", "a|b" or NULL (no board needed) */
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
int installed_list_light(const struct image *im, struct installed **out, size_t *n);
int installed_each(const struct image *im, void (*fn)(struct installed *, void *), void *arg);
void installed_free(struct installed *p, size_t n);

/* a manifest from the repository, checked against the catalog's SHA-1 */
int remote_manifest(const struct image *im, const char *stem, const char *version,
    const char *sig, struct buf *out);

/* --- install.c ------------------------------------------------------------- */
extern int dry_run;
extern unsigned long min_free_mb;
int journal_recover(const struct image *im);
int cmd_install(struct image *im, int argc, char **argv, int update);
int cmd_uninstall(struct image *im, int argc, char **argv);
int cmd_autoremove(struct image *im);
int cmd_history(const struct image *im);

#endif
