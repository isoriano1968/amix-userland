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

#include "pkg.h"

#define TMP	".pkgtmp"

int dry_run;
unsigned long min_free_mb = 200;

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
	struct buf raw;			/* as fetched, written to installed/ */
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
	if (remote_manifest(im, stem, v->version, v->sig, &c->raw) < 0 ||
	    manifest_parse(c->raw.data, c->raw.len, &c->m) < 0)
		return -1;
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
			return -1;
		c = plan_find(p, stem);	/* the array may have moved */
	}
	return 0;
}

static void
plan_free(struct plan *p)
{
	size_t i;

	for (i = 0; i < p->n; i++) {
		free(p->c[i].stem);
		free(p->c[i].version);
		manifest_free(&p->c[i].m);
		buf_free(&p->c[i].raw);
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
	struct buf b = { 0 };
	char h[65];
	int same = 0;

	if (want_hash && read_file(full, &b) == 0) {
		sha512_256_hex(b.data, b.len, h);
		same = !strcmp(h, want_hash);
	}
	buf_free(&b);
	return same;
}

/* the payload of a file action, checked */
static int
payload(const struct image *im, const struct action *a, struct buf *out)
{
	char *rel = xasprintf("publisher/%s/file/%.2s/%s", PUBLISHER, a->payload, a->payload);
	struct buf gz = { 0 };
	const char *want = content_hash(a, "file"), *sz = action_get(a, "pkg.size");
	char h[65];
	int r = -1;

	if (fetch(im->origin, rel, &gz) < 0)
		warnx("%s: cannot fetch %s: %s", action_get(a, "path"), a->payload, strerror(errno));
	else if (gunzip(&gz, out) < 0)
		warnx("%s: payload is not gzip", action_get(a, "path"));
	else if (sz && out->len != parse_ull(sz))
		warnx("%s: payload size %lu, should be %s", action_get(a, "path"),
		    (unsigned long)out->len, sz);
	else {
		if (want)
			sha512_256_hex(out->data, out->len, h);
		else
			sha1_hex(out->data, out->len, h);
		if (strcmp(h, want ? want : a->payload))
			warnx("%s: payload does not match its hash", action_get(a, "path"));
		else
			r = 0;
	}
	buf_free(&gz);
	free(rel);
	return r;
}

static int
write_tmp(const char *full, const struct buf *data)
{
	char *tmp = xasprintf("%s" TMP, full);
	int r = write_atomic(tmp, data->data, data->len) ;

	/* write_atomic went via tmp.new; tmp is now the whole, synced file */
	free(tmp);
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
					struct buf b = { 0 };

					if (payload(im, a, &b) < 0) {
						bad = 1;
					} else {
						char *fn = root_path(im->root, np);
						char *npt = xasprintf("%s" TMP, np);

						txn_tmp(t, npt);
						if (write_tmp(fn, &b) < 0) {
							warnx("%s: %s", np, strerror(errno));
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
					buf_free(&b);
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
				struct buf b = { 0 };

				if (payload(im, a, &b) < 0)
					bad = 1;
				else {
					txn_tmp(t, tmp);
					if (write_tmp(full, &b) < 0) {
						warnx("%s: %s", p, strerror(errno));
						bad = 1;
					} else {
						char *ft = xasprintf("%s" TMP, full);

						set_attrs(im, ft, a);
						free(ft);
						txn_op(t, OP_RENAME, tmp, p);
					}
				}
				buf_free(&b);
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
static void
queue_rmdirs(const struct manifest *oldm, const struct manifest *newm, const struct owners *w,
    const char *stem, struct txn *t)
{
	size_t i;

	for (i = oldm->n; i-- > 0;) {
		const struct action *a = &oldm->act[i];
		const char *p = action_get(a, "path");

		if (strcmp(a->type, "dir") || !p || (newm && find_path(newm, p)))
			continue;
		if (owners_find(w, p, stem, 0))
			continue;		/* another package, or AMIX itself, has it */
		txn_op(t, OP_RMDIR, p, NULL);
	}
}

/* --- checks ------------------------------------------------------------------ */

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

/* the sorted list of every path the client owns */
static void
paths_write(const struct image *im)
{
	struct installed *in;
	size_t nin, i, k;
	struct buf b = { 0 };
	char *f = image_path(im, "paths");
	struct owners w = { 0 };

	installed_list(im, &in, &nin);
	for (i = 0; i < nin; i++)
		for (k = 0; k < in[i].m.n; k++) {
			const char *p = action_get(&in[i].m.act[k], "path");

			if (p)
				owners_add(&w, p, in[i].stem, in[i].m.act[k].type[0]);
		}
	qsort(w.o, w.n, sizeof(*w.o), cmp_owner);
	buf_add(&b, "", 0);
	for (i = 0; i < w.n; i++) {
		buf_str(&b, w.o[i].path);
		buf_str(&b, "\t");
		buf_str(&b, w.o[i].stem);
		buf_str(&b, "\n");
	}
	write_atomic(f, b.data, b.len);
	buf_free(&b);
	owners_free(&w);
	installed_free(in, nin);
	free(f);
}

/* --- the commands ------------------------------------------------------------ */

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
			queue_rmdirs(&c->old->m, c->version ? &c->m : NULL, &w, c->stem, &t);
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

					txn_tmp(&t, tmpf);
					if (write_atomic(full, c->raw.data, c->raw.len) < 0) {
						warnx("%s: %s", tmpf, strerror(errno));
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

int
cmd_install(struct image *im, int argc, char **argv, int update)
{
	struct installed *in;
	size_t nin, i;
	struct catalog cat;
	struct plan p = { 0 };
	int r = 1;

	if (lock(im) < 0)
		return 1;
	if (journal_recover(im) < 0)
		return 1;
	if (catalog_load(im, &cat) < 0) {
		warnx("no catalog: pkg refresh");
		return 1;
	}
	installed_list(im, &in, &nin);
	if (update && argc == 0) {
		for (i = 0; i < nin; i++)
			if (plan_add(im, &cat, in, nin, &p, in[i].stem, NULL, NULL, 1) < 0)
				goto out;
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
	if (!p.n) {
		printf("nothing to do\n");
		r = 0;
		goto out;
	}
	r = run(im, update ? "update" : "install", &p, in, nin, 0);
out:
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
	int r = 1;

	if (argc == 0) {
		warnx("uninstall what?");
		return 2;
	}
	if (lock(im) < 0 || journal_recover(im) < 0)
		return 1;
	installed_list(im, &in, &nin);
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
out:
	plan_free(&p);
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
