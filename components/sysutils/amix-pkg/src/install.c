/*
 * install.c -- install, update and uninstall (IPS-INSTALLER-PLAN.md step 3).
 *
 * One transaction:
 *   1. resolve: the packages named, their requires from the catalog;
 *   2. check: conflicts (other IPS packages, AMIX's SVR4 contents, files
 *      nobody owns), free space;
 *   3. prepare: journal "prepare"; every new file and link is written as
 *      <path>.pkgtmp and checked (size, SHA-512/256), new directories made;
 *   4. commit: the journal is rewritten as "commit" with every rename and
 *      removal; then they run, and the journal goes.
 * Interrupted in 3: the next run removes the .pkgtmp files (roll back).
 * Interrupted in 4: the next run does the rest of the journal (roll forward).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <zlib.h>

#include "pkg.h"

#define TMP	".pkgtmp"

int dry_run;
unsigned long min_free_mb = 100;

static void
crash_point(const char *where)
{
#ifdef PKG_TESTING
	const char *c = getenv("PKG_TEST_CRASH");

	if (c && !strcmp(c, where)) {
		fprintf(stderr, "pkg: test crash at %s\n", where);
		_exit(99);
	}
#else
	(void)where;
#endif
}

/* --- the lock ---------------------------------------------------------------- */

static char *lockfile;

static void
unlock(void)
{
	if (lockfile) {
		unlink(lockfile);
		free(lockfile);
		lockfile = NULL;
	}
}

static int
lock(const struct image *im)
{
	char *dir = image_path(im, "");
	char pid[32];
	int fd, tries;

	mkdirs(dir, 0755);
	free(dir);
	lockfile = image_path(im, "lock");
	for (tries = 0; tries < 2; tries++) {
		fd = open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd >= 0) {
			snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
			write(fd, pid, strlen(pid));
			close(fd);
			atexit(unlock);
			return 0;
		}
		{
			struct buf b = { 0 };
			long other = 0;

			if (read_file(lockfile, &b) == 0)
				other = strtol(b.data, NULL, 10);
			buf_free(&b);
			if (other > 0 && kill((pid_t)other, 0) == 0) {
				warnx("another pkg (pid %ld) is working on this image", other);
				break;
			}
			unlink(lockfile);	/* a lock left by a pkg that is gone */
		}
	}
	free(lockfile);
	lockfile = NULL;
	return -1;
}

/* --- the journal ------------------------------------------------------------- */

enum opkind { OP_RENAME, OP_REMOVE, OP_SAVE, OP_RMDIR };

struct op {
	enum opkind kind;
	char *a, *b;		/* image-relative paths; b for RENAME only */
};

struct txn {
	char **tmp;		/* .pkgtmp files written in prepare */
	size_t ntmp;
	char **newdir;		/* directories made in prepare */
	size_t nnewdir;
	struct op *op;
	size_t nop;
	struct buf msgs;	/* what to tell the user afterwards */
};

/* while preparing, every .pkgtmp file and new directory is in the journal
 * (appended and synced) before it is made, so a crash leaves none unknown */
static int jfd = -1;

static void
journal_note(const char *kind, const char *p)
{
	char *line;

	if (jfd < 0)
		return;
	line = xasprintf("%s\t%s\n", kind, p);
	if (write(jfd, line, strlen(line)) < 0 || fsync(jfd) < 0)
		die("cannot write the journal: %s", strerror(errno));
	free(line);
}

static void
txn_tmp(struct txn *t, const char *p)
{
	journal_note("tmp", p);
	t->tmp = xrealloc(t->tmp, (t->ntmp + 1) * sizeof(*t->tmp));
	t->tmp[t->ntmp++] = xstrdup(p);
}

static void
txn_newdir(struct txn *t, const char *p)
{
	journal_note("newdir", p);
	t->newdir = xrealloc(t->newdir, (t->nnewdir + 1) * sizeof(*t->newdir));
	t->newdir[t->nnewdir++] = xstrdup(p);
}

static void
txn_op(struct txn *t, enum opkind k, const char *a, const char *b)
{
	t->op = xrealloc(t->op, (t->nop + 1) * sizeof(*t->op));
	t->op[t->nop].kind = k;
	t->op[t->nop].a = xstrdup(a);
	t->op[t->nop].b = b ? xstrdup(b) : NULL;
	t->nop++;
}

static void
txn_free(struct txn *t)
{
	size_t i;

	for (i = 0; i < t->ntmp; i++)
		free(t->tmp[i]);
	for (i = 0; i < t->nnewdir; i++)
		free(t->newdir[i]);
	for (i = 0; i < t->nop; i++) {
		free(t->op[i].a);
		free(t->op[i].b);
	}
	free(t->tmp);
	free(t->newdir);
	free(t->op);
	buf_free(&t->msgs);
	memset(t, 0, sizeof(*t));
}

static int
journal_write(const struct image *im, const struct txn *t, int commit)
{
	struct buf b = { 0 };
	char *j = image_path(im, "journal");
	size_t i;
	int r;
	static const char *const kn[] = { "rename", "remove", "save", "rmdir" };

	buf_str(&b, commit ? "state\tcommit\n" : "state\tprepare\n");
	for (i = 0; i < t->ntmp; i++) {
		buf_str(&b, "tmp\t");
		buf_str(&b, t->tmp[i]);
		buf_str(&b, "\n");
	}
	for (i = 0; i < t->nnewdir; i++) {
		buf_str(&b, "newdir\t");
		buf_str(&b, t->newdir[i]);
		buf_str(&b, "\n");
	}
	for (i = 0; commit && i < t->nop; i++) {
		buf_str(&b, kn[t->op[i].kind]);
		buf_str(&b, "\t");
		buf_str(&b, t->op[i].a);
		if (t->op[i].b) {
			buf_str(&b, "\t");
			buf_str(&b, t->op[i].b);
		}
		buf_str(&b, "\n");
	}
	r = write_atomic(j, b.data, b.len);
	buf_free(&b);
	free(j);
	return r;
}

/* one journal operation; each may run twice (roll forward after a crash) */
static int
do_op(const struct image *im, const struct op *o)
{
	char *a = root_path(im->root, o->a), *b = o->b ? root_path(im->root, o->b) : NULL;
	struct stat st;
	int r = 0;

	switch (o->kind) {
	case OP_RENAME:
		if (lstat(a, &st) == 0 && rename(a, b) < 0)
			r = -1;
		break;
	case OP_REMOVE:
		if (unlink(a) < 0 && errno != ENOENT)
			r = -1;
		break;
	case OP_SAVE: {
		char *s = xasprintf("%s.pkgsave", a);

		if (lstat(a, &st) == 0 && rename(a, s) < 0)
			r = -1;
		free(s);
		break;
	}
	case OP_RMDIR:
		rmdir(a);		/* only if empty: what others put there stays */
		break;
	}
	if (r < 0)
		warnx("%s: %s", o->a, strerror(errno));
	free(a);
	free(b);
	return r;
}

static int
journal_remove(const struct image *im)
{
	char *j = image_path(im, "journal");
	int r = unlink(j);

	free(j);
	return r;
}

/* finish or undo what an interrupted transaction left */
int
journal_recover(const struct image *im)
{
	char *j = image_path(im, "journal");
	struct buf b = { 0 };
	char *line, *nl;
	int commit = 0, bad = 0;
	struct txn t = { 0 };

	if (read_file(j, &b) < 0) {
		free(j);
		return 0;
	}
	for (line = b.data; line && *line; line = nl ? nl + 1 : NULL) {
		char *f1, *f2;

		nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		f1 = strchr(line, '\t');
		if (!f1)
			continue;
		*f1++ = 0;
		f2 = strchr(f1, '\t');
		if (f2)
			*f2++ = 0;
		if (!strcmp(line, "state"))
			commit = !strcmp(f1, "commit");
		else if (!strcmp(line, "tmp"))
			txn_tmp(&t, f1);
		else if (!strcmp(line, "newdir"))
			txn_newdir(&t, f1);
		else if (!strcmp(line, "rename") && f2)
			txn_op(&t, OP_RENAME, f1, f2);
		else if (!strcmp(line, "remove"))
			txn_op(&t, OP_REMOVE, f1, NULL);
		else if (!strcmp(line, "save"))
			txn_op(&t, OP_SAVE, f1, NULL);
		else if (!strcmp(line, "rmdir"))
			txn_op(&t, OP_RMDIR, f1, NULL);
	}
	buf_free(&b);
	if (commit) {
		size_t i;

		warnx("finishing an interrupted change (%lu steps)", (unsigned long)t.nop);
		for (i = 0; i < t.nop; i++)
			bad |= do_op(im, &t.op[i]) < 0;
	} else {
		size_t i;

		warnx("undoing an interrupted change (%lu files)", (unsigned long)t.ntmp);
		for (i = 0; i < t.ntmp; i++) {
			char *p = root_path(im->root, t.tmp[i]), *pn = xasprintf("%s.new", p);

			unlink(p);
			unlink(pn);		/* write_atomic's own temporary */
			free(pn);
			free(p);
		}
		for (i = t.nnewdir; i-- > 0;) {
			char *p = root_path(im->root, t.newdir[i]);

			rmdir(p);
			free(p);
		}
	}
	txn_free(&t);
	if (!bad)
		unlink(j);
	free(j);
	return bad ? -1 : 0;
}

/* --- who owns what ----------------------------------------------------------- */

struct owner {
	char *path;
	const char *stem;	/* installed package, or SVR4_OWNER */
	char type;		/* 'd' directory, 'f' other */
};

static int
cmp_owner(const void *a, const void *b)
{
	return strcmp(((const struct owner *)a)->path, ((const struct owner *)b)->path);
}

struct owners {
	struct owner *o;
	size_t n;
};

static void
owners_add(struct owners *w, const char *path, const char *stem, char type)
{
	w->o = xrealloc(w->o, (w->n + 1) * sizeof(*w->o));
	w->o[w->n].path = xstrdup(path);
	w->o[w->n].stem = stem;
	w->o[w->n].type = type;
	w->n++;
}

/* the first non-directory owner of path other than skip, or any dir owner */
static const struct owner *
owners_find(const struct owners *w, const char *path, const char *skip, int want_file)
{
	struct owner key;
	const struct owner *hit;
	size_t lo = 0, hi = w->n, i;

	key.path = (char *)path;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;

		if (cmp_owner(&w->o[mid], &key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (i = lo, hit = NULL; i < w->n && !strcmp(w->o[i].path, path); i++) {
		if (skip && !strcmp(w->o[i].stem, skip))
			continue;
		if (want_file && w->o[i].type == 'd')
			continue;
		hit = &w->o[i];
		break;
	}
	return hit;
}

static const char SVR4_OWNER[] = "an AMIX (SVR4) package";

static void
owners_load(const struct image *im, const struct installed *in, size_t nin, struct owners *w)
{
	size_t i, k;
	char *f = root_path(im->root, "var/sadm/install/contents");
	struct buf b = { 0 };

	memset(w, 0, sizeof(*w));
	for (i = 0; i < nin; i++)
		for (k = 0; k < in[i].m.n; k++) {
			const struct action *a = &in[i].m.act[k];
			const char *p = action_get(a, "path");

			if (p)
				owners_add(w, p, in[i].stem, !strcmp(a->type, "dir") ? 'd' : 'f');
		}
	/* /usr/bin/ls f none 0555 bin bin ...   /usr/lib/x=../y s none ... */
	if (read_file(f, &b) == 0) {
		char *line, *nl;

		for (line = b.data; line && *line; line = nl ? nl + 1 : NULL) {
			char *sp, *eq;

			nl = strchr(line, '\n');
			if (nl)
				*nl = 0;
			if (*line != '/' || !(sp = strchr(line, ' ')))
				continue;
			*sp = 0;
			if ((eq = strchr(line, '=')))
				*eq = 0;
			owners_add(w, line + 1, SVR4_OWNER, sp[1] == 'd' ? 'd' : 'f');
		}
	}
	buf_free(&b);
	free(f);
	qsort(w->o, w->n, sizeof(*w->o), cmp_owner);
}

static void
owners_free(struct owners *w)
{
	size_t i;

	for (i = 0; i < w->n; i++)
		free(w->o[i].path);
	free(w->o);
}

/* --- resolving --------------------------------------------------------------- */

struct change {
	char *stem;
	char *version;			/* new version, NULL: uninstall */
	const struct installed *old;	/* NULL: fresh install */
	struct manifest m;		/* new manifest */
	char *sig;			/* its catalog SHA-1: fetched again at commit, as
					 * the raw text is not kept (memory) */
};

struct plan {
	struct change *c;
	size_t n;
};

static struct change *
plan_find(struct plan *p, const char *stem)
{
	size_t i;

	for (i = 0; i < p->n; i++)
		if (!strcmp(p->c[i].stem, stem))
			return &p->c[i];
	return NULL;
}

static const struct installed *
inst_find(const struct installed *in, size_t n, const char *stem)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (!strcmp(in[i].stem, stem))
			return &in[i];
	return NULL;
}

static const struct catpkg *
cat_match(const struct catalog *cat, const char *name)
{
	size_t i, nl = strlen(name);
	const struct catpkg *hit = NULL;

	for (i = 0; i < cat->n; i++) {
		const char *s = cat->pkg[i].stem;
		size_t l = strlen(s);

		if (!strcmp(s, name))
			return &cat->pkg[i];
		if (l > nl && s[l - nl - 1] == '/' && !strcmp(s + l - nl, name)) {
			if (hit) {
				warnx("%s: more than one package matches (%s, %s)", name, hit->stem, s);
				return NULL;
			}
			hit = &cat->pkg[i];
		}
	}
	return hit;
}

/*
 * A version this machine cannot run (org.amix.cpu 68060 without a 68060,
 * org.amix.hw without the board): plan_add gives -2.  In a whole update
 * (pkg update, no names) such a package keeps its installed version
 * ("held back") and the rest goes on; the reason is kept for the message.
 */
#define REFUSED (-2)
static int refuse_quiet;
static char refuse_why[200];

static void
refuse(const char *stem, const char *why)
{
	snprintf(refuse_why, sizeof(refuse_why), "%s", why);
	if (!refuse_quiet)
		warnx("%s: %s", stem, why);
}

/* add stem (at least version min, or exactly want) and what it requires */
static int
plan_add(const struct image *im, const struct catalog *cat, const struct installed *in,
    size_t nin, struct plan *p, const char *stem, const char *min, const char *want,
    int update)
{
	const struct catpkg *cp = catalog_find(cat, stem);
	const struct installed *old = inst_find(in, nin, stem);
	const struct catver *v = NULL;
	struct change *c;
	size_t i;

	if (plan_find(p, stem))
		return 0;
	if (old && !want && !update && (!min || version_at_least(old->version, min)))
		return 0;		/* installed and good enough */
	if (!cp || !cp->n) {
		warnx("%s: not in the catalog", stem);
		return -1;
	}
	for (i = cp->n; i-- > 0;) {
		if (want && strncmp(cp->v[i].version, want, strlen(want)))
			continue;
		if (min && !version_at_least(cp->v[i].version, min))
			continue;
		v = &cp->v[i];
		break;
	}
	if (!v) {
		warnx("%s: no version %s%s in the catalog", stem, want ? want : ">= ",
		    want ? "" : min ? min : "?");
		return -1;
	}
	if (old && !strcmp(old->version, v->version))
		return 0;		/* that very version is installed */
	if (old && update && version_cmp(v->version, old->version) < 0)
		return 0;		/* never downgrade by update */
	p->c = xrealloc(p->c, (p->n + 1) * sizeof(*p->c));
	c = &p->c[p->n++];
	memset(c, 0, sizeof(*c));
	c->stem = xstrdup(stem);
	c->version = xstrdup(v->version);
	c->old = old;
	{
		struct buf raw = { 0 };
		int bad = remote_manifest(im, stem, v->version, v->sig, &raw) < 0 ||
		    manifest_parse(raw.data, raw.len, &c->m) < 0;

		buf_free(&raw);
		if (bad)
			return -1;
		c->sig = v->sig ? xstrdup(v->sig) : NULL;
	}
	{
		/* org.amix.cpu 68060: code a 68020/030 cannot run ("Bad System
		 * Call" on its first 68040/060 instruction) */
		const char *cpu = manifest_set(&c->m, "org.amix.cpu");

		if (cpu && !strcmp(cpu, "68060") && !cpu_has_060()) {
			refuse(stem, "built for the 68060 only (org.amix.cpu), and this machine has none");
			return REFUSED;
		}
	}
	{
		/* org.amix.hw: a board it needs (amix-hw, PKG_HW in tests) */
		const char *hw = manifest_set(&c->m, "org.amix.hw");

		if (hw && !hw_has(hw)) {
			char why[160];

			snprintf(why, sizeof(why), "needs the board %s (org.amix.hw), and this machine has %s",
			    hw, *hw_list() ? hw_list() : "none");
			refuse(stem, why);
			return REFUSED;
		}
	}
	for (i = 0; i < c->m.n; i++) {
		const struct action *a = &c->m.act[i];
		const char *fm = action_get(a, "fmri"), *ty = action_get(a, "type");
		struct fmri f;
		int r;

		if (strcmp(a->type, "depend") || !fm || (ty && strcmp(ty, "require")))
			continue;
		if (fmri_parse(fm, &f) < 0)
			continue;
		r = plan_add(im, cat, in, nin, p, f.stem, f.version, NULL, 0);
		fmri_free(&f);
		if (r < 0)
			return r;	/* REFUSED stays REFUSED: the whole update holds it back */
		c = plan_find(p, stem);	/* the array may have moved */
	}
	return 0;
}

static void
plan_cut(struct plan *p, size_t n)	/* drop the changes from n on */
{
	while (p->n > n) {
		struct change *c = &p->c[--p->n];

		free(c->stem);
		free(c->version);
		manifest_free(&c->m);
		free(c->sig);
	}
}

static void
plan_free(struct plan *p)
{
	size_t i;

	for (i = 0; i < p->n; i++) {
		free(p->c[i].stem);
		free(p->c[i].version);
		manifest_free(&p->c[i].m);
		free(p->c[i].sig);
	}
	free(p->c);
}

/* --- preparing files ---------------------------------------------------------- */

static long
id_of(const struct image *im, const char *file, const char *name)
{
	char *f = root_path(im->root, file);
	struct buf b = { 0 };
	long id = -1;
	char *line, *nl;
	size_t l = strlen(name);

	if (read_file(f, &b) == 0)
		for (line = b.data; line && *line; line = nl ? nl + 1 : NULL) {
			nl = strchr(line, '\n');
			if (!strncmp(line, name, l) && line[l] == ':') {
				char *p = strchr(line + l + 1, ':');

				if (p)
					id = strtol(p + 1, NULL, 10);
				break;
			}
		}
	buf_free(&b);
	free(f);
	return id;
}

static void
set_attrs(const struct image *im, const char *full, const struct action *a)
{
	const char *mode = action_get(a, "mode"), *o = action_get(a, "owner");
	const char *g = action_get(a, "group");

	if (geteuid() == 0 && (o || g)) {	/* only root can give files away */
		long uid = o ? id_of(im, "etc/passwd", o) : -1;
		long gid = g ? id_of(im, "etc/group", g) : -1;

		if (chown(full, (uid_t)uid, (gid_t)gid) < 0)
			warnx("%s: chown: %s", full, strerror(errno));
	}
	if (mode)
		chmod(full, (mode_t)strtoul(mode, NULL, 8));
}

static int
same_content(const char *full, const char *want_hash)
{
	char h[65];

	return want_hash && hash_file(full, HASH_SHA512_256, h) == 0 && !strcmp(h, want_hash);
}

/* a payload on its way to a file: gunzip, hash and write, piece by piece */
struct pstream {
	z_stream z;
	struct hash h;
	int fd, end, bad;
	unsigned long long n;
};

static int
pstream_sink(const void *p, size_t n, void *arg)
{
	struct pstream *ps = arg;
	unsigned char out[16384];

	ps->z.next_in = (unsigned char *)p;
	ps->z.avail_in = (uInt)n;
	while (ps->z.avail_in && !ps->end) {
		int r;
		size_t got;

		ps->z.next_out = out;
		ps->z.avail_out = sizeof(out);
		r = inflate(&ps->z, Z_NO_FLUSH);
		if (r != Z_OK && r != Z_STREAM_END) {
			ps->bad = 1;
			errno = EINVAL;
			return -1;
		}
		got = sizeof(out) - ps->z.avail_out;
		hash_update(&ps->h, out, got);
		ps->n += got;
		while (got) {
			ssize_t w = write(ps->fd, out + (sizeof(out) - ps->z.avail_out - got), got);

			if (w <= 0)
				return -1;
			got -= (size_t)w;
		}
		if (r == Z_STREAM_END)
			ps->end = 1;
	}
	return 0;
}

/*
 * the payload of a file action into dest (a new file), checked: gunzipped
 * and hashed as it arrives, so no payload is ever whole in memory (an
 * A3000 has 16 MB). 0, or -1 after a message (dest removed).
 */
static int
payload_to(const struct image *im, const struct action *a, const char *dest)
{
	char *rel = xasprintf("publisher/%s/file/%.2s/%s", PUBLISHER, a->payload, a->payload);
	const char *want = content_hash(a, "file"), *sz = action_get(a, "pkg.size");
	const char *path = action_get(a, "path");
	struct pstream ps;
	char h[65];
	int r = -1;

	memset(&ps, 0, sizeof(ps));
	hash_init(&ps.h, want ? HASH_SHA512_256 : HASH_SHA1);
	ps.fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (ps.fd < 0) {
		warnx("%s: %s", dest, strerror(errno));
		free(rel);
		return -1;
	}
	if (inflateInit2(&ps.z, 16 + MAX_WBITS) != Z_OK) {
		warnx("%s: zlib: no memory", path);
		close(ps.fd);
		unlink(dest);
		free(rel);
		return -1;
	}
	if (fetch_stream(im->origin, rel, pstream_sink, &ps) < 0) {
		if (ps.bad)
			warnx("%s: payload is not gzip", path);
		else
			warnx("%s: cannot fetch %s: %s", path, a->payload, strerror(errno));
	} else if (!ps.end)
		warnx("%s: payload is cut short", path);
	else if (sz && ps.n != parse_ull(sz))
		warnx("%s: payload size %llu, should be %s", path, ps.n, sz);
	else {
		hash_final(&ps.h, h);
		if (strcmp(h, want ? want : a->payload))
			warnx("%s: payload does not match its hash", path);
		else if (fsync(ps.fd) < 0)
			warnx("%s: %s", dest, strerror(errno));
		else
			r = 0;
	}
	inflateEnd(&ps.z);
	if (close(ps.fd) < 0 && r == 0) {
		warnx("%s: %s", dest, strerror(errno));
		r = -1;
	}
	if (r < 0)
		unlink(dest);
	free(rel);
	return r;
}

/* the payload as full TMP (written as full TMP.new, then renamed): 0 or -1 */
static int
write_payload(const struct image *im, const struct action *a, const char *full)
{
	char *tmp = xasprintf("%s" TMP, full), *nw = xasprintf("%s" TMP ".new", full);
	int r = payload_to(im, a, nw);

	if (r == 0 && rename(nw, tmp) < 0) {
		warnx("%s: %s", tmp, strerror(errno));
		unlink(nw);
		r = -1;
	}
	free(tmp);
	free(nw);
	return r;
}

static const struct action *
find_path(const struct manifest *m, const char *path)
{
	size_t i;

	for (i = 0; m && i < m->n; i++) {
		const char *p = action_get(&m->act[i], "path");

		if (p && !strcmp(p, path))
			return &m->act[i];
	}
	return NULL;
}

static int
wanted(const struct image *im, const struct action *a)
{
	const char *f = action_get(a, "facet.devel");

	return im->devel || !f || strcmp(f, "true");
}

/* prepare one package: .pkgtmp files, directories; queue renames/removals */
static int
prepare(const struct image *im, struct change *c, struct txn *t,
    unsigned long long *bytes)
{
	const struct manifest *oldm = c->old ? &c->old->m : NULL;
	size_t i;
	int bad = 0;

	/* directories first, parents before children (manifests list them so) */
	for (i = 0; i < c->m.n; i++) {
		const struct action *a = &c->m.act[i];
		const char *p = action_get(a, "path");
		char *full;
		struct stat st;

		if (strcmp(a->type, "dir") || !p || !wanted(im, a))
			continue;
		full = root_path(im->root, p);
		if (lstat(full, &st) < 0) {
			if (!dry_run) {
				txn_newdir(t, p);
				if (mkdir(full, 0755) < 0) {
					warnx("%s: %s", p, strerror(errno));
					bad = 1;
				} else
					set_attrs(im, full, a);	/* new: the package's; old: kept */
			}
		} else if (!S_ISDIR(st.st_mode) && !link_to_dir(full, &st)) {
			warnx("%s: should be a directory", p);
			bad = 1;
		}
		free(full);
	}
	if (bad)
		return -1;
	for (i = 0; i < c->m.n; i++) {
		const struct action *a = &c->m.act[i];
		const char *p = action_get(a, "path");
		char *full, *tmp;
		struct stat st;
		int exists;

		if (!p || !wanted(im, a) || !strcmp(a->type, "dir"))
			continue;
		full = root_path(im->root, p);
		tmp = xasprintf("%s" TMP, p);
		exists = lstat(full, &st) == 0;
		if (!strcmp(a->type, "file")) {
			const struct action *olda = find_path(oldm, p);
			const char *want = content_hash(a, "file"), *pres = action_get(a, "preserve");
			const char *sz = action_get(a, "pkg.size");

			if (olda && content_hash(olda, "file") && want &&
			    !strcmp(content_hash(olda, "file"), want) && same_content(full, want)) {
				set_attrs(im, full, a);		/* unchanged: attributes only */
				free(full);
				free(tmp);
				continue;
			}
			if (pres && exists && !same_content(full, want) &&
			    !(olda && same_content(full, content_hash(olda, "file")))) {
				/* the user changed it: keep theirs, the package's beside it */
				char *np = xasprintf("%s.new", p);

				if (!dry_run) {
					char *fn = root_path(im->root, np);
					char *npt = xasprintf("%s" TMP, np);

					txn_tmp(t, npt);
					if (write_payload(im, a, fn) < 0) {
						bad = 1;
					} else {
						char *ft = xasprintf("%s" TMP, fn);

						set_attrs(im, ft, a);
						free(ft);
						txn_op(t, OP_RENAME, npt, np);
					}
					free(npt);
					free(fn);
				}
				buf_str(&t->msgs, "kept /");
				buf_str(&t->msgs, p);
				buf_str(&t->msgs, "; the package's version is /");
				buf_str(&t->msgs, np);
				buf_str(&t->msgs, "\n");
				free(np);
				free(full);
				free(tmp);
				continue;
			}
			if (sz)
				*bytes += parse_ull(sz);
			if (!dry_run) {
				txn_tmp(t, tmp);
				if (write_payload(im, a, full) < 0)
					bad = 1;
				else {
					char *ft = xasprintf("%s" TMP, full);

					set_attrs(im, ft, a);
					free(ft);
					txn_op(t, OP_RENAME, tmp, p);
				}
			}
		} else if (!strcmp(a->type, "link")) {
			const char *target = action_get(a, "target");
			char cur[4096];
			ssize_t n;

			if (exists && S_ISLNK(st.st_mode) && target &&
			    (n = readlink(full, cur, sizeof(cur) - 1)) >= 0 &&
			    (cur[n] = 0, !strcmp(cur, target))) {
				free(full);
				free(tmp);
				continue;
			}
			if (!dry_run && target) {
				char *ft = xasprintf("%s" TMP, full);

				unlink(ft);
				txn_tmp(t, tmp);
				if (symlink(target, ft) < 0) {
					warnx("%s: %s", p, strerror(errno));
					bad = 1;
				} else
					txn_op(t, OP_RENAME, tmp, p);
				free(ft);
			}
		} else if (!strcmp(a->type, "hardlink")) {
			const char *target = action_get(a, "target");

			if (!dry_run && target) {
				char *ft = xasprintf("%s" TMP, full), *tf, *tft;

				if (*target == '/')
					tf = root_path(im->root, target);
				else {
					char *d = xstrdup(full), *sl = strrchr(d, '/');

					if (sl)
						*sl = 0;
					tf = xasprintf("%s/%s", d, target);
					free(d);
				}
				tft = xasprintf("%s" TMP, tf);
				unlink(ft);
				txn_tmp(t, tmp);
				/* the target may be new in this transaction: link its .pkgtmp */
				if (link(access(tft, F_OK) == 0 ? tft : tf, ft) < 0) {
					warnx("%s: %s", p, strerror(errno));
					bad = 1;
				} else
					txn_op(t, OP_RENAME, tmp, p);
				free(tf);
				free(tft);
				free(ft);
			}
		}
		free(full);
		free(tmp);
	}
	/* what the old version had and the new one has not */
	for (i = 0; oldm && i < oldm->n; i++) {
		const struct action *a = &oldm->act[i];
		const char *p = action_get(a, "path");

		if (!p || find_path(&c->m, p))
			continue;
		if (!strcmp(a->type, "file")) {
			char *full = root_path(im->root, p);
			const char *h = content_hash(a, "file");

			if (access(full, F_OK) == 0 && h && !same_content(full, h)) {
				txn_op(t, OP_SAVE, p, NULL);
				buf_str(&t->msgs, "changed /");
				buf_str(&t->msgs, p);
				buf_str(&t->msgs, " kept as /");
				buf_str(&t->msgs, p);
				buf_str(&t->msgs, ".pkgsave\n");
			} else
				txn_op(t, OP_REMOVE, p, NULL);
			free(full);
		} else if (!strcmp(a->type, "dir"))
			;			/* after the files, below */
		else
			txn_op(t, OP_REMOVE, p, NULL);
	}
	return bad ? -1 : 0;
}

/* old directories nobody needs any more, deepest first */
static int plan_wants(const struct plan *p, const char *path);

static void
queue_rmdirs(const struct manifest *oldm, const struct manifest *newm, const struct owners *w,
    const char *stem, const struct plan *pl, struct txn *t)
{
	size_t i;

	for (i = oldm->n; i-- > 0;) {
		const struct action *a = &oldm->act[i];
		const char *p = action_get(a, "path");

		if (strcmp(a->type, "dir") || !p || (newm && find_path(newm, p)))
			continue;
		if (owners_find(w, p, stem, 0) || plan_wants(pl, p))
			continue;		/* another package, or AMIX itself, has it */
		txn_op(t, OP_RMDIR, p, NULL);
	}
}

/* --- checks ------------------------------------------------------------------ */

/* a path one of the new versions of this plan delivers */
static int
plan_wants(const struct plan *p, const char *path)
{
	size_t i;

	for (i = 0; p && i < p->n; i++)
		if (p->c[i].version && find_path(&p->c[i].m, path))
			return 1;
	return 0;
}

/* the owner is itself updated or removed in this plan, and its new version
 * no longer delivers the path: the path moves between packages (2026-11-20:
 * XErrorDB from desktop/cde to x11/x11r6.3 and back) */
static int
moves_away(const struct plan *p, const char *owner, const char *path)
{
	size_t i;

	for (i = 0; i < p->n; i++)
		if (!strcmp(p->c[i].stem, owner))
			return !p->c[i].version || !find_path(&p->c[i].m, path);
	return 0;
}

static int
conflicts(const struct image *im, const struct plan *p, const struct owners *w)
{
	size_t i, k;
	int bad = 0;

	for (i = 0; i < p->n; i++) {
		const struct change *c = &p->c[i];

		for (k = 0; k < c->m.n; k++) {
			const struct action *a = &c->m.act[k];
			const char *path = action_get(a, "path");
			const struct owner *o;
			char *full;
			struct stat st;

			if (!path || !wanted(im, a) || !strcmp(a->type, "dir"))
				continue;
			o = owners_find(w, path, c->stem, 1);
			if (o && moves_away(p, o->stem, path))
				continue;	/* removed before it is renamed in */
			if (o) {
				warnx("/%s (%s) belongs to %s", path, c->stem, o->stem);
				bad = 1;
				continue;
			}
			if (c->old && find_path(&c->old->m, path))
				continue;	/* its own older version */
			full = root_path(im->root, path);
			if (lstat(full, &st) == 0 && !S_ISDIR(st.st_mode) &&
			    !action_get(a, "preserve")) {
				warnx("/%s (%s) is there already and no package owns it", path, c->stem);
				bad = 1;
			} else if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
				warnx("/%s (%s) is a directory", path, c->stem);
				bad = 1;
			}
			free(full);
		}
		/* two packages of this change wanting one path */
		for (k = i + 1; k < p->n; k++) {
			size_t x;

			for (x = 0; x < p->c[k].m.n; x++) {
				const struct action *a = &p->c[k].m.act[x];
				const char *path = action_get(a, "path");
				const struct action *b;

				if (!path || !strcmp(a->type, "dir"))
					continue;
				b = find_path(&c->m, path);
				if (b && strcmp(b->type, "dir")) {
					warnx("/%s is in %s and %s", path, c->stem, p->c[k].stem);
					bad = 1;
				}
			}
		}
	}
	return bad ? -1 : 0;
}

/*
 * Space per file system: each file counts where it lands (its nearest existing
 * directory, symlinks followed: /usr/local may live on a data disk), and every
 * file system it touches keeps min_free_mb free.
 */
struct fsuse {
	dev_t dev;
	char *where;			/* an existing directory on it */
	unsigned long long bytes;
};

static void
fs_add(struct fsuse **u, size_t *n, const struct image *im, const char *path,
    unsigned long long bytes)
{
	char *d = root_path(im->root, path), *s;
	struct stat st;
	size_t i;

	for (;;) {
		if ((s = strrchr(d, '/')) == NULL || s == d) {
			strcpy(d, "/");
			break;
		}
		*s = 0;
		if (stat(d, &st) == 0 && S_ISDIR(st.st_mode))
			break;
	}
	if (stat(d, &st) < 0) {
		free(d);
		return;
	}
	for (i = 0; i < *n; i++)
		if ((*u)[i].dev == st.st_dev) {
			(*u)[i].bytes += bytes;
			free(d);
			return;
		}
	*u = xrealloc(*u, (*n + 1) * sizeof(**u));
	(*u)[*n].dev = st.st_dev;
	(*u)[*n].where = d;
	(*u)[*n].bytes = bytes;
	(*n)++;
}

static int
space_ok(const struct image *im, struct fsuse *u, size_t n)
{
	size_t i;
	int ok = 1;

	if (n == 0)
		fs_add(&u, &n, im, "/.", 0);	/* nothing to add: still the root's rule */
	for (i = 0; i < n; i++) {
		struct statvfs sv;
		unsigned long long avail, need;

		need = u[i].bytes + (unsigned long long)min_free_mb * 1024 * 1024;
		if (statvfs(u[i].where, &sv) < 0)
			continue;	/* cannot tell: do not stop */
		avail = (unsigned long long)sv.f_bavail * (sv.f_frsize ? sv.f_frsize : sv.f_bsize);
		if (avail < need) {
			warnx("not enough space on the file system of %s: %lu MB free, %lu MB "
			    "needed (%lu MB must stay free)", u[i].where, (unsigned long)(avail >> 20),
			    (unsigned long)(u[i].bytes >> 20), min_free_mb);
			ok = 0;
		}
	}
	for (i = 0; i < n; i++)
		free(u[i].where);
	free(u);
	return ok;
}

static void
history(const struct image *im, const char *cmd, const struct plan *p, const char *result)
{
	char *f = image_path(im, "history");
	FILE *fp = fopen(f, "a");
	time_t now = time(NULL);
	char ts[32];
	size_t i;

	if (fp) {
		strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
		fprintf(fp, "%s\t%s\t%s\t", ts, cmd, result);
		for (i = 0; i < p->n; i++) {
			char *v = p->c[i].version ? version_short(p->c[i].version) : NULL;

			fprintf(fp, "%s%s%s%s", i ? " " : "", p->c[i].stem, v ? "@" : "", v ? v : "");
			free(v);
		}
		fputc('\n', fp);
		fclose(fp);
	}
	free(f);
}

/* paths_write: the owners table, and the stems it points to (kept until written) */
struct pathsw {
	struct owners w;
	char **stems;
	size_t ns;
};

/* one installed package's paths into the table; its manifest freed at once */
static void
paths_one(struct installed *in, void *arg)
{
	struct pathsw *pw = arg;
	size_t k;

	for (k = 0; k < in->m.n; k++) {
		const char *p = action_get(&in->m.act[k], "path");

		if (p)
			owners_add(&pw->w, p, in->stem, in->m.act[k].type[0]);
	}
	pw->stems = xrealloc(pw->stems, (pw->ns + 1) * sizeof(*pw->stems));
	pw->stems[pw->ns++] = in->stem;
	manifest_free(&in->m);
	free(in->version);
}

/* var/pkg/paths: every installed path and its package, sorted */
static void
paths_write(const struct image *im)
{
	size_t i;
	struct buf b = { 0 };
	char *f = image_path(im, "paths");
	struct pathsw pw;

	memset(&pw, 0, sizeof(pw));
	installed_each(im, paths_one, &pw);	/* one manifest in memory at a time */
	qsort(pw.w.o, pw.w.n, sizeof(*pw.w.o), cmp_owner);
	buf_add(&b, "", 0);
	for (i = 0; i < pw.w.n; i++) {
		buf_str(&b, pw.w.o[i].path);
		buf_str(&b, "\t");
		buf_str(&b, pw.w.o[i].stem);
		buf_str(&b, "\n");
	}
	write_atomic(f, b.data, b.len);
	buf_free(&b);
	owners_free(&pw.w);
	for (i = 0; i < pw.ns; i++)
		free(pw.stems[i]);
	free(pw.stems);
	free(f);
}

/* --- the commands ------------------------------------------------------------ */

/*
 * Removals (and .pkgsave renames) first, then the renames into place, then the
 * empty directories: a path one package of the plan drops and another brings
 * is then removed before the new file arrives, never after. Stable within each
 * kind; the journal keeps this order, so a roll forward does the same.
 */
static void
order_ops(struct txn *t)
{
	static const int rank[] = { 1, 0, 0, 2 };	/* RENAME REMOVE SAVE RMDIR */
	struct op *o = xmalloc(t->nop * sizeof(*o) + 1);
	size_t i, n = 0;
	int r;

	for (r = 0; r < 3; r++)
		for (i = 0; i < t->nop; i++)
			if (rank[t->op[i].kind] == r)
				o[n++] = t->op[i];
	free(t->op);
	t->op = o;
}

/* every installed package that owns a path of the new versions: updated as
 * well, if the catalog has a newer version (it may have dropped the path) */
static int
plan_owners(const struct image *im, const struct catalog *cat, const struct installed *in,
    size_t nin, struct plan *p)
{
	struct owners w;
	size_t i, k;
	int r = 0;

	owners_load(im, in, nin, &w);
	for (i = 0; r == 0 && i < p->n; i++)	/* p->n grows: the added ones too */
		for (k = 0; r == 0 && p->c[i].version && k < p->c[i].m.n; k++) {
			const char *path = action_get(&p->c[i].m.act[k], "path");
			const struct owner *o;
			char *s;

			if (!path || !strcmp(p->c[i].m.act[k].type, "dir"))
				continue;
			o = owners_find(&w, path, p->c[i].stem, 1);
			if (!o || o->stem == SVR4_OWNER || plan_find(p, o->stem))
				continue;
			s = xstrdup(o->stem);
			{
				size_t n0 = p->n;

				r = plan_add(im, cat, in, nin, p, s, NULL, NULL, 1);
				if (r == REFUSED) {	/* it stays as it is: conflicts() decides */
					plan_cut(p, n0);
					r = 0;
				}
			}
			free(s);
		}
	owners_free(&w);
	return r;
}

/* the running pkg itself is updated alone first, then run again: the rest of
 * the plan is then done by the new pkg (which may know things the old one
 * does not, as moving a file between packages) */
#define SELF_STEM "sysutils/amix-pkg"
int putenv(char *);		/* XSI: hidden by _POSIX_C_SOURCE on the host */
char **self_argv;		/* main's argv, for the run again */

static int
run(struct image *im, const char *cmd, struct plan *p, const struct installed *in,
    size_t nin, int removing)
{
	struct owners w;
	struct txn t;
	unsigned long long bytes = 0;
	size_t i;
	int r = 1;

	(void)nin;
	memset(&t, 0, sizeof(t));
	owners_load(im, in, nin, &w);
	if (!removing && conflicts(im, p, &w) < 0) {
		warnx("nothing was changed");
		goto out;
	}
	for (i = 0; i < p->n; i++) {
		char *v = p->c[i].version ? version_short(p->c[i].version) : NULL;
		char *ov = p->c[i].old ? version_short(p->c[i].old->version) : NULL;

		printf("%-10s %s%s%s%s%s\n", !p->c[i].version ? "remove" : ov ? "update" : "install",
		    p->c[i].stem, ov ? " " : "", ov ? ov : "", v ? (ov ? " -> " : " ") : "", v ? v : "");
		free(v);
		free(ov);
	}
	if (!removing) {		/* an upper bound: every file of the new versions */
		struct fsuse *u = NULL;
		size_t k, nu = 0;

		for (i = 0; i < p->n; i++)
			for (k = 0; k < p->c[i].m.n; k++) {
				const struct action *a = &p->c[i].m.act[k];
				const char *sz = action_get(a, "pkg.size"), *path = action_get(a, "path");

				if (!strcmp(a->type, "file") && sz && path && wanted(im, a))
					fs_add(&u, &nu, im, path, parse_ull(sz));
			}
		if (!space_ok(im, u, nu)) {
			warnx("nothing was changed");
			goto out;
		}
		bytes = 0;
	}
	if (!dry_run) {
		char *j = image_path(im, "journal");

		if (journal_write(im, &t, 0) < 0 || (jfd = open(j, O_WRONLY | O_APPEND)) < 0) {
			warnx("cannot write the journal: %s", strerror(errno));
			free(j);
			goto out;
		}
		free(j);
	}
	for (i = 0; i < p->n; i++) {
		struct change *c = &p->c[i];

		if (c->version && prepare(im, c, &t, &bytes) < 0)
			goto undo;
		if (!c->version) {		/* uninstall: every path of the old manifest */
			size_t k;

			for (k = 0; k < c->old->m.n; k++) {
				const struct action *a = &c->old->m.act[k];
				const char *path = action_get(a, "path");

				if (!path || !strcmp(a->type, "dir"))
					continue;
				if (!strcmp(a->type, "file")) {
					char *full = root_path(im->root, path);
					const char *h = content_hash(a, "file");

					if (access(full, F_OK) == 0 && h && !same_content(full, h)) {
						txn_op(&t, OP_SAVE, path, NULL);
						buf_str(&t.msgs, "changed /");
						buf_str(&t.msgs, path);
						buf_str(&t.msgs, " kept as /");
						buf_str(&t.msgs, path);
						buf_str(&t.msgs, ".pkgsave\n");
					} else
						txn_op(&t, OP_REMOVE, path, NULL);
					free(full);
				} else
					txn_op(&t, OP_REMOVE, path, NULL);
			}
		}
		if (c->old)
			queue_rmdirs(&c->old->m, c->version ? &c->m : NULL, &w, c->stem, p, &t);
		/* the installed manifest, renamed with the files */
		{
			char *qs = url_quote(c->stem);
			char *rel = xasprintf("var/pkg/installed/%s", qs);

			if (c->version) {
				char *dir = image_path(im, "installed"), *tmpf;

				if (!dry_run)
					mkdirs(dir, 0755);
				free(dir);
				tmpf = xasprintf("%s" TMP, rel);
				if (!dry_run) {
					char *full = root_path(im->root, tmpf);

					struct buf raw = { 0 };
					int bad;

					txn_tmp(&t, tmpf);
					bad = remote_manifest(im, c->stem, c->version, c->sig, &raw) < 0 ||
					    write_atomic(full, raw.data, raw.len) < 0;
					if (bad && errno)
						warnx("%s: %s", tmpf, strerror(errno));
					buf_free(&raw);
					if (bad) {
						free(full);
						free(tmpf);
						free(rel);
						free(qs);
						goto undo;
					}
					free(full);
				}
				txn_op(&t, OP_RENAME, tmpf, rel);
				free(tmpf);
			} else
				txn_op(&t, OP_REMOVE, rel, NULL);
			free(rel);
			free(qs);
		}
	}
	if (jfd >= 0) {
		close(jfd);
		jfd = -1;
	}
	if (dry_run) {
		printf("%lu files, %lu kB to write (dry run: nothing changed)\n",
		    (unsigned long)t.ntmp, (unsigned long)(bytes >> 10));
		r = 0;
		goto out;
	}
	crash_point("prepared");
	order_ops(&t);
	if (journal_write(im, &t, 1) < 0) {
		warnx("cannot write the journal: %s", strerror(errno));
		goto undo;
	}
	/* the commit point: from here on, a crash is finished by the next run */
	for (i = 0; i < t.nop; i++) {
		do_op(im, &t.op[i]);
		if (i == t.nop / 2)
			crash_point("committing");
	}
	journal_remove(im);
	/* committed: the owners and the new manifests are done with (history needs
	 * only names and versions), so paths_write does not stack on top of them */
	owners_free(&w);
	memset(&w, 0, sizeof(w));
	for (i = 0; i < p->n; i++)
		manifest_free(&p->c[i].m);
	paths_write(im);
	history(im, cmd, p, "ok");
	if (t.msgs.len)
		fputs(t.msgs.data, stdout);
	r = 0;
	goto out;
undo:
	if (jfd >= 0) {
		close(jfd);
		jfd = -1;
	}
	if (!dry_run) {
		journal_write(im, &t, 0);
		journal_recover(im);
		history(im, cmd, p, "failed");
	}
	warnx("nothing was changed");
out:
	owners_free(&w);
	txn_free(&t);
	return r;
}

/* does another installed package require stem? */
static int
needed_by_other(const struct installed *in, size_t nin, const char *stem)
{
	size_t k, a;

	for (k = 0; k < nin; k++) {
		if (!strcmp(in[k].stem, stem))
			continue;
		for (a = 0; a < in[k].m.n; a++) {
			const struct action *ac = &in[k].m.act[a];
			const char *fm = action_get(ac, "fmri"), *ty = action_get(ac, "type");
			struct fmri f;
			int hit;

			if (strcmp(ac->type, "depend") || !fm || (ty && strcmp(ty, "require")) ||
			    fmri_parse(fm, &f) < 0)
				continue;
			hit = !strcmp(f.stem, stem);
			fmri_free(&f);
			if (hit)
				return 1;
		}
	}
	return 0;
}

/*
 * var/pkg/requested: the packages pkg install was asked for by name (since
 * 0.5.3), one stem a line; what they pulled in is not there.  pkg
 * autoremove takes away what nothing requires and nobody asked for.
 */
static int
requested_has(const struct buf *b, const char *stem)
{
	const char *s = b->data;
	size_t l = strlen(stem);

	while (s && *s) {
		if (!strncmp(s, stem, l) && (s[l] == '\n' || s[l] == 0))
			return 1;
		s = strchr(s, '\n');
		if (s)
			s++;
	}
	return 0;
}

static void
requested_change(const struct image *im, char **add, size_t nadd, char **del, size_t ndel)
{
	char *f = image_path(im, "requested");
	struct buf b = { 0 }, o = { 0 };
	const char *s, *e;
	size_t i;

	if (dry_run) {
		free(f);
		return;
	}
	if (read_file(f, &b) < 0)
		buf_add(&b, "", 0);
	buf_add(&o, "", 0);
	for (s = b.data; s && *s; s = e ? e + 1 : NULL) {
		int gone = 0;
		size_t l;

		e = strchr(s, '\n');
		l = e ? (size_t)(e - s) : strlen(s);
		for (i = 0; i < ndel; i++)
			if (strlen(del[i]) == l && !strncmp(s, del[i], l))
				gone = 1;
		if (!gone && l) {
			buf_add(&o, s, l);
			buf_str(&o, "\n");
		}
	}
	for (i = 0; i < nadd; i++)
		if (!requested_has(&o, add[i])) {
			buf_str(&o, add[i]);
			buf_str(&o, "\n");
		}
	write_atomic(f, o.data, o.len);
	buf_free(&b);
	buf_free(&o);
	free(f);
}

/*
 * Names with a pattern (gnu/ then a star, a star then bash) become every stem that matches
 * (stem_match: the whole stem or a part after a "/"): from the catalog to
 * install, from what is installed to update or uninstall.  Plain names stay
 * as they are.  NULL: a pattern matched nothing (said).
 */
static char **
expand_names(const struct catalog *cat, const struct installed *in, size_t nin, int argc,
    char **argv, int from_installed, int *nout)
{
	char **out = NULL;
	int i, n = 0;
	size_t k;

	for (i = 0; i < argc; i++) {
		int hits = 0;

		if (!has_glob(argv[i])) {
			out = xrealloc(out, (n + 1) * sizeof(*out));
			out[n++] = xstrdup(argv[i]);
			continue;
		}
		if (from_installed) {
			for (k = 0; k < nin; k++)
				if (stem_match(argv[i], in[k].stem)) {
					out = xrealloc(out, (n + 1) * sizeof(*out));
					out[n++] = xstrdup(in[k].stem);
					hits++;
				}
		} else if (cat) {
			for (k = 0; k < cat->n; k++)
				if (stem_match(argv[i], cat->pkg[k].stem)) {
					out = xrealloc(out, (n + 1) * sizeof(*out));
					out[n++] = xstrdup(cat->pkg[k].stem);
					hits++;
				}
		}
		if (!hits) {
			warnx("%s: matches no %s package", argv[i], from_installed ? "installed" : "known");
			while (n > 0)
				free(out[--n]);
			free(out);
			return NULL;
		}
	}
	*nout = n;
	return out;
}

static void
names_free(char **v, int n)
{
	while (n > 0)
		free(v[--n]);
	free(v);
}

int
cmd_install(struct image *im, int argc, char **argv, int update)
{
	struct installed *in;
	size_t nin, i;
	struct catalog cat;
	struct plan p = { 0 };
	int r = 1, nheld = 0;
	char **names = NULL;
	int nnames = 0;

	if (lock(im) < 0)
		return 1;
	if (journal_recover(im) < 0)
		return 1;
	if (catalog_load(im, &cat) < 0) {
		warnx("no catalog: pkg refresh");
		return 1;
	}
	installed_list(im, &in, &nin);
	if (argc > 0) {
		if (!(names = expand_names(&cat, in, nin, argc, argv, update, &nnames)))
			goto out;
		argc = nnames;
		argv = names;
	}
	if (update && argc == 0) {
		for (i = 0; i < nin; i++) {
			size_t n0 = p.n;
			int ra;

			refuse_quiet = 1;
			ra = plan_add(im, &cat, in, nin, &p, in[i].stem, NULL, NULL, 1);
			refuse_quiet = 0;
			if (ra == REFUSED) {
				plan_cut(&p, n0);
				{
					char *v = version_short(in[i].version);

					printf("held back  %s %s: its newest version %s\n", in[i].stem,
					    v ? v : in[i].version, refuse_why);
					free(v);
				}
				nheld++;
				if (!needed_by_other(in, nin, in[i].stem))
					printf("           nothing installed needs it: pkg uninstall %s\n",
					    in[i].stem);
			} else if (ra < 0)
				goto out;
		}
	}
	for (i = 0; i < (size_t)argc; i++) {
		struct fmri f;
		const struct catpkg *cp;

		if (fmri_parse(argv[i], &f) < 0 || !(cp = cat_match(&cat, f.stem))) {
			if (f.stem)
				warnx("%s: not in the catalog", argv[i]);
			fmri_free(&f);
			goto out;
		}
		if (update && !inst_find(in, nin, cp->stem)) {
			warnx("%s: not installed (pkg install)", cp->stem);
			fmri_free(&f);
			goto out;
		}
		if (plan_add(im, &cat, in, nin, &p, cp->stem, NULL, f.version, update) < 0) {
			fmri_free(&f);
			goto out;
		}
		fmri_free(&f);
	}
	if (plan_owners(im, &cat, in, nin, &p) < 0)
		goto out;
	if (!p.n) {
		printf(nheld ? "nothing else to do\n" : "nothing to do\n");
		r = 0;
		goto out;
	}
	if (p.n > 1 && plan_find(&p, SELF_STEM) && inst_find(in, nin, SELF_STEM) &&
	    self_argv && !getenv("PKG_SELF_UPDATED")) {
		struct plan s = { 0 };

		printf("pkg updates itself first\n");
		if (plan_add(im, &cat, in, nin, &s, SELF_STEM, NULL, NULL, 1) < 0 ||
		    run(im, update ? "update" : "install", &s, in, nin, 0) != 0) {
			plan_free(&s);
			goto out;
		}
		plan_free(&s);
		if (dry_run) {
			printf("(dry run: the rest after pkg's own update)\n");
			r = 0;
			goto out;
		}
		unlock();
		putenv((char *)"PKG_SELF_UPDATED=1");	/* AMIX has no setenv */
		fflush(NULL);
		if (!strcmp(im->root, "/"))
			execv("/usr/bin/pkg", self_argv);
		execvp(self_argv[0], self_argv);
		warnx("cannot run pkg again (%s): %s; run the same command again",
		    self_argv[0], strerror(errno));
		goto out;
	}
	r = run(im, update ? "update" : "install", &p, in, nin, 0);
	if (r == 0 && !update && argc > 0) {
		char **names = xmalloc(argc * sizeof(*names));
		int k, nn = 0;

		for (k = 0; k < argc; k++) {
			struct fmri f;
			const struct catpkg *cp;

			if (fmri_parse(argv[k], &f) == 0 && (cp = cat_match(&cat, f.stem)))
				names[nn++] = cp->stem;
			fmri_free(&f);
		}
		requested_change(im, names, nn, NULL, 0);
		free(names);
	}
out:
	names_free(names, nnames);
	plan_free(&p);
	installed_free(in, nin);
	catalog_free(&cat);
	return r;
}

int
cmd_uninstall(struct image *im, int argc, char **argv)
{
	struct installed *in;
	size_t nin, i, k;
	struct plan p = { 0 };
	int r = 1, nnames = 0;
	char **names;

	if (argc == 0) {
		warnx("uninstall what?");
		return 2;
	}
	if (lock(im) < 0 || journal_recover(im) < 0)
		return 1;
	installed_list(im, &in, &nin);
	if (!(names = expand_names(NULL, in, nin, argc, argv, 1, &nnames))) {
		installed_free(in, nin);
		return 1;
	}
	argc = nnames;
	argv = names;
	for (i = 0; i < (size_t)argc; i++) {
		const struct installed *ip = NULL;
		size_t nl = strlen(argv[i]);

		for (k = 0; k < nin; k++) {
			size_t l = strlen(in[k].stem);

			if (!strcmp(in[k].stem, argv[i]) ||
			    (l > nl && in[k].stem[l - nl - 1] == '/' && !strcmp(in[k].stem + l - nl, argv[i])))
				ip = &in[k];
		}
		if (!ip) {
			warnx("%s: not installed", argv[i]);
			goto out;
		}
		p.c = xrealloc(p.c, (p.n + 1) * sizeof(*p.c));
		memset(&p.c[p.n], 0, sizeof(p.c[p.n]));
		p.c[p.n].stem = xstrdup(ip->stem);
		p.c[p.n].old = ip;
		p.n++;
	}
	/* nothing installed may still need what goes */
	for (k = 0; k < nin; k++) {
		size_t a;

		if (plan_find(&p, in[k].stem))
			continue;
		for (a = 0; a < in[k].m.n; a++) {
			const struct action *ac = &in[k].m.act[a];
			const char *fm = action_get(ac, "fmri");
			struct fmri f;

			if (strcmp(ac->type, "depend") || !fm || fmri_parse(fm, &f) < 0)
				continue;
			if (plan_find(&p, f.stem)) {
				warnx("%s needs %s: not removed", in[k].stem, f.stem);
				fmri_free(&f);
				goto out;
			}
			fmri_free(&f);
		}
	}
	r = run(im, "uninstall", &p, in, nin, 1);
	if (r == 0) {
		char **names = xmalloc((p.n + 1) * sizeof(*names));

		for (k = 0; k < p.n; k++)
			names[k] = p.c[k].stem;
		requested_change(im, NULL, 0, names, p.n);
		free(names);
	}
out:
	names_free(names, nnames);
	plan_free(&p);
	installed_free(in, nin);
	return r;
}

/*
 * pkg autoremove [-n]: what nothing installed requires and nobody asked for
 * (var/pkg/requested), pass after pass until nothing more goes (each pass
 * against what was installed at its start); never pkg itself.  Without the
 * record (an image from before 0.5.3) only the true leaves are listed, one
 * pass, with what marks a likely one (its newest version cannot run here,
 * a library), and nothing is removed.
 */
int
cmd_autoremove(struct image *im)
{
	struct installed *in;
	size_t nin, i;
	struct buf req = { 0 };
	char *f = image_path(im, "requested");
	char **gone = NULL;
	size_t ngone = 0;
	int have_req = read_file(f, &req) == 0, more = 1, r = 0;

	free(f);
	if (!have_req)
		buf_add(&req, "", 0);
	installed_list(im, &in, &nin);
	while (more) {
		size_t pass_gone = ngone;	/* the needed test looks at the start of the pass */

		more = 0;
		for (i = 0; i < nin; i++) {
			size_t k, a;
			int needed = 0, out = 0;

			for (k = 0; k < ngone; k++)
				if (!strcmp(gone[k], in[i].stem))
					out = 1;
			if (out || !strcmp(in[i].stem, SELF_STEM) || requested_has(&req, in[i].stem))
				continue;
			for (k = 0; k < nin && !needed; k++) {
				int kgone = 0;

				for (a = 0; a < pass_gone; a++)
					if (!strcmp(gone[a], in[k].stem))
						kgone = 1;
				if (k == i || kgone)
					continue;
				for (a = 0; a < in[k].m.n && !needed; a++) {
					const struct action *ac = &in[k].m.act[a];
					const char *fm = action_get(ac, "fmri"), *ty = action_get(ac, "type");
					struct fmri fr;

					if (strcmp(ac->type, "depend") || !fm || (ty && strcmp(ty, "require")) ||
					    fmri_parse(fm, &fr) < 0)
						continue;
					needed = !strcmp(fr.stem, in[i].stem);
					fmri_free(&fr);
				}
			}
			if (!needed) {
				gone = xrealloc(gone, (ngone + 1) * sizeof(*gone));
				gone[ngone++] = in[i].stem;
				more = 1;
			}
		}
		if (!have_req)
			more = 0;		/* leaves only: their requirements may be wanted */
	}
	if (!ngone)
		printf("nothing to remove\n");
	else if (!have_req) {
		struct catalog cat;
		int have_cat = catalog_load(im, &cat) == 0;

		printf("nothing installed requires these; pkg has no record of what you asked for\n"
		    "(it keeps one from 0.5.3 on): remove the ones you do not want with pkg uninstall\n");
		for (i = 0; i < ngone; i++) {
			const struct catpkg *cp = have_cat ? catalog_find(&cat, gone[i]) : NULL;
			const struct catver *v = cp && cp->n ? &cp->v[cp->n - 1] : NULL;
			const char *why = "";
			char b[120];

			if (v && v->hw && !hw_has(v->hw)) {
				snprintf(b, sizeof(b), "   (its newest version needs the board %s)", v->hw);
				why = b;
			} else if (v && v->cpu && !strcmp(v->cpu, "68060") && !cpu_has_060())
				why = "   (its newest version is 68060 only)";
			else if (!strncmp(gone[i], "library/", 8))
				why = "   (a library: probably pulled in once)";
			printf("  %s%s\n", gone[i], why);
		}
		if (have_cat)
			catalog_free(&cat);
	} else {
		char **names = xmalloc(ngone * sizeof(*names));

		for (i = 0; i < ngone; i++)
			names[i] = xstrdup(gone[i]);
		installed_free(in, nin);
		in = NULL;
		nin = 0;
		r = cmd_uninstall(im, (int)ngone, names);
		for (i = 0; i < ngone; i++)
			free(names[i]);
		free(names);
	}
	free(gone);
	buf_free(&req);
	if (in)
		installed_free(in, nin);
	return r;
}

int
cmd_history(const struct image *im)
{
	char *f = image_path(im, "history");
	struct buf b = { 0 };

	if (read_file(f, &b) == 0)
		fputs(b.data, stdout);
	buf_free(&b);
	free(f);
	return 0;
}
