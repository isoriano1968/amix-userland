/*
 * pkg -- the AMIX IPS client (IPS-INSTALLER-PLAN.md): set-publisher,
 * publisher, refresh, list, info, contents, verify (step 2); install,
 * update, uninstall, history (step 3, install.c).
 */
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pkg.h"

#define VERSION_STRING	"0.5.1"

static struct image im;

static void
usage(void)
{
	fprintf(stderr,
	    "usage: pkg [-R root] [-v] command ...\n"
	    "  set-publisher -O origin [%s]   where packages come from (http://, file://, a folder)\n"
	    "     -g origin / -G origin|'*'      add one (a mirror) / remove one or all\n"
	    "     --enable-origin / --disable-origin origin   use it, or keep it unused\n"
	    "  unset-publisher [" PUBLISHER "]       forget every origin\n"
	    "  publisher [-t]                  the origins (-t: tab-separated)\n"
	    "  refresh                         fetch the catalog (the first origin that answers)\n"
	    "  list [-a] [-u] [-H] [-t] [name...]  installed packages (-a all, -u updates,\n"
	    "                                  -t tab-separated with CPU and summary)\n"
	    "  search [-t] text                packages whose name or summary holds text\n"
	    "  info [-r] name[@version]...      about a package (-r from the repository)\n"
	    "  contents [-r] [-m] [-t type] name  its paths (-m the manifest)\n"
	    "  verify [-q] [name...]           check installed files against their manifests\n"
	    "  install [-n] name[@version]...  with what they require (-n: only show)\n"
	    "  update [-n] [name...]           to the newest versions (all installed: no names)\n"
	    "  uninstall [-n] name...          remove; changed files stay as .pkgsave\n"
	    "  history                         what was changed, and when\n"
	    "  http get URL [FILE] | put URL   plain HTTP (the installer's bootstrap)\n"
	    "  version\n"
	    "--min-free MB: space that must stay free on each file system it writes to (default 100)\n", PUBLISHER);
	exit(2);
}

/* getopt for subcommands: argv[0] is the command */
static int optidx;

static int
subopt(int argc, char **argv, const char *opts, char **arg)
{
	static int pos;
	char c;
	const char *o;

	if (optidx == 0) {
		optidx = 1;
		pos = 0;
	}
	if (optidx >= argc || argv[optidx][0] != '-' || !argv[optidx][1])
		return -1;
	if (!strcmp(argv[optidx], "--")) {
		optidx++;
		return -1;
	}
	c = argv[optidx][++pos];
	if (!(o = strchr(opts, c)))
		usage();
	if (o[1] == ':') {
		if (argv[optidx][pos + 1])
			*arg = argv[optidx] + pos + 1;
		else if (optidx + 1 < argc)
			*arg = argv[++optidx];
		else
			usage();
		optidx++;
		pos = 0;
	} else if (!argv[optidx][pos + 1]) {
		optidx++;
		pos = 0;
	}
	return c;
}

static const char *
short_name(const char *stem)
{
	return stem;
}

static int
need_catalog(struct catalog *c)
{
	if (catalog_load(&im, c) < 0) {
		warnx("no catalog: pkg refresh");
		return -1;
	}
	return 0;
}

static const struct installed *
find_installed(const struct installed *in, size_t n, const char *stem)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (!strcmp(in[i].stem, stem))
			return &in[i];
	return NULL;
}

static int
match(const char *stem, int argc, char **argv)
{
	int i;

	if (argc <= 0)
		return 1;
	for (i = 0; i < argc; i++) {
		const char *p = argv[i];
		size_t l = strlen(stem), pl = strlen(p);

		if (!strncmp(p, "pkg:/", 5))
			p += 5;
		pl = strlen(p);
		/* whole stem, or its last part ("hello" for demo/hello) */
		if (!strcmp(stem, p) || (l > pl && stem[l - pl - 1] == '/' && !strcmp(stem + l - pl, p)))
			return 1;
	}
	return 0;
}

/* --- set-publisher, publisher, refresh -------------------------------------- */

static int
cmd_set_publisher(int argc, char **argv)
{
	char *origin = NULL, *arg, **av;
	int c, ac = 0, i, changed = 0;

	/* the long options first; the rest goes through subopt */
	av = xmalloc((argc + 1) * sizeof(*av));
	for (i = 0; i < argc; i++) {
		if ((!strcmp(argv[i], "--enable-origin") || !strcmp(argv[i], "--disable-origin")) &&
		    i + 1 < argc) {
			if (image_origin_off(&im, argv[i + 1], argv[i][2] == 'd') < 0)
				die("%s: not an origin of %s (pkg publisher)", argv[i + 1], PUBLISHER);
			changed = 1;
			i++;
		} else
			av[ac++] = argv[i];
	}
	av[ac] = NULL;
	while ((c = subopt(ac, av, "O:g:G:", &arg)) != -1) {
		if (c == 'O') {
			/* -O: this origin and no other, as pkg(5); -g after it adds mirrors */
			image_remove_origin(&im, "*");
			image_add_origin(&im, arg);
			free(im.origin);
			im.origin = xstrdup(arg);
			origin = arg;
			changed = 1;
		} else if (c == 'g') {
			image_add_origin(&im, arg);
			if (!im.origin)
				im.origin = xstrdup(arg);
			changed = 1;
		} else {
			if (image_remove_origin(&im, arg) < 0)
				die("%s: not an origin of %s (pkg publisher)", arg, PUBLISHER);
			changed = 1;
		}
	}
	if (optidx < ac && strcmp(av[optidx], PUBLISHER))
		die("the publisher is %s", PUBLISHER);
	(void)origin;
	free(av);
	if (!changed)
		usage();
	if (image_save_cfg(&im) < 0)
		die("cannot write %s/var/pkg/cfg: %s", im.root, strerror(errno));
	return 0;
}

static int
cmd_unset_publisher(int argc, char **argv)
{
	if (argc > 2 || (argc == 2 && strcmp(argv[1], PUBLISHER)))
		die("the publisher is %s", PUBLISHER);
	image_remove_origin(&im, "*");
	if (image_save_cfg(&im) < 0)
		die("cannot write %s/var/pkg/cfg: %s", im.root, strerror(errno));
	return 0;
}

/* publisher [-t]: one line per origin; -t: publisher, origin, on|off, active|- */
static int
cmd_publisher(int argc, char **argv)
{
	int tabs = 0, c;
	char *arg;
	size_t i;

	while ((c = subopt(argc, argv, "t", &arg)) != -1)
		tabs = 1;
	if (!tabs)
		printf("PUBLISHER  TYPE     STATUS  ORIGIN\n");
	if (!im.norigins && !tabs)
		printf("%-10s origin   -       (none)\n", PUBLISHER);
	for (i = 0; i < im.norigins; i++) {
		int act = im.origin && !strcmp(im.origin, im.origins[i]);

		if (tabs)
			printf("%s\t%s\t%s\t%s\n", PUBLISHER, im.origins[i], im.off[i] ? "off" : "on",
			    act ? "active" : "-");
		else
			printf("%-10s origin   %-7s %s%s\n", PUBLISHER, im.off[i] ? "off" : "on",
			    im.origins[i], act ? "  (in use)" : "");
	}
	return 0;
}

static int
cmd_refresh(void)
{
	struct catalog c;
	size_t i, nv = 0;

	if (catalog_refresh(&im) < 0)
		return 1;
	if (catalog_load(&im, &c) == 0) {
		for (i = 0; i < c.n; i++)
			nv += c.pkg[i].n;
		if (verbose)
			printf("%lu packages, %lu versions\n", (unsigned long)c.n, (unsigned long)nv);
		catalog_free(&c);
	}
	return 0;
}

/* --- list ------------------------------------------------------------------- */

static int list_or_search(int argc, char **argv, const char *text);

/* does s hold text, ignoring case (AMIX's libc has no strcasestr) */
static int
holds(const char *s, const char *text)
{
	size_t n = strlen(text);

	for (; s && *s; s++) {
		size_t k;

		for (k = 0; k < n && s[k] && tolower((unsigned char)s[k]) ==
		    tolower((unsigned char)text[k]); k++)
			;
		if (k == n)
			return 1;
	}
	return n == 0;
}

/*
 * one package for -t, tab-separated (the AMIX Software Center reads it):
 * name, version (the installed one, else the newest), flags (i installed,
 * u an update is there), cpu (org.amix.cpu or -), the newest version, what
 * it requires (stems, comma-separated, or -), the summary
 */
static void
tab_line(const char *stem, const char *version, int inst, int newer, const struct catver *cv)
{
	char *sv = version_short(version), *nv = cv ? version_short(cv->version) : NULL;
	char **d;
	int first = 1;

	printf("%s\t%s\t%c%c\t%s\t%s\t", stem, sv, inst ? 'i' : '-', newer ? 'u' : '-',
	    cv && cv->cpu ? cv->cpu : "-", nv ? nv : "-");
	for (d = cv ? cv->depends : NULL; d && *d; d++) {
		const char *s = *d, *at;

		if (!strncmp(s, "pkg://", 6) && (at = strchr(s + 6, '/')))
			s = at + 1;
		else if (!strncmp(s, "pkg:/", 5))
			s += 5;
		at = strchr(s, '@');
		printf("%s%.*s", first ? "" : ",", (int)(at ? at - s : (long)strlen(s)), s);
		first = 0;
	}
	printf("%s\t%s\n", first ? "-" : "", cv && cv->summary ? cv->summary : "");
	free(sv);
	free(nv);
}

static int
cmd_list(int argc, char **argv)
{
	return list_or_search(argc, argv, NULL);
}

static int
cmd_search(int argc, char **argv)
{
	int c, tabs = 0;
	char *arg, *av[3];

	while ((c = subopt(argc, argv, "t", &arg)) != -1)
		tabs = 1;
	if (optidx + 1 != argc)
		usage();
	av[0] = "search";
	av[1] = tabs ? "-at" : "-a";
	av[2] = NULL;
	{
		char *text = argv[optidx];

		optidx = 0;
		return list_or_search(2, av, text);
	}
}

static int
list_or_search(int argc, char **argv, const char *text)
{
	int all = 0, upd = 0, noheader = 0, tabs = 0, c, found = 0;
	char *arg;
	struct installed *in;
	size_t nin, i;
	struct catalog cat;
	int have_cat;

	while ((c = subopt(argc, argv, "auHt", &arg)) != -1) {
		if (c == 'a')
			all = 1;
		else if (c == 'u')
			upd = 1;
		else if (c == 't')
			tabs = noheader = 1;
		else
			noheader = 1;
	}
	argc -= optidx;
	argv += optidx;
	installed_list(&im, &in, &nin);
	have_cat = catalog_load(&im, &cat) == 0;
	if ((all || upd) && !have_cat) {
		installed_free(in, nin);
		warnx("no catalog: pkg refresh");
		return 1;
	}
	if (!noheader)
		printf("%-36s %-22s %s\n", "NAME (" PUBLISHER ")", "VERSION", "IFO");
	if (all) {
		for (i = 0; i < cat.n; i++) {
			const struct catpkg *p = &cat.pkg[i];
			const struct installed *ip = find_installed(in, nin, p->stem);
			const char *v = ip ? ip->version : p->v[p->n - 1].version;
			char *sv;

			const struct catver *cv = p->n ? &p->v[p->n - 1] : NULL;

			if (!p->n || !match(p->stem, argc, argv))
				continue;
			if (text && !holds(p->stem, text) && !holds(cv->summary, text))
				continue;
			found = 1;
			if (tabs) {
				tab_line(p->stem, v, ip != NULL,
				    ip && version_cmp(cv->version, ip->version) > 0, cv);
				continue;
			}
			sv = version_short(v);
			printf("%-36s %-22s %c--\n", short_name(p->stem), sv, ip ? 'i' : '-');
			free(sv);
		}
	} else {
		for (i = 0; i < nin; i++) {
			const struct catpkg *p = have_cat ? catalog_find(&cat, in[i].stem) : NULL;
			int newer = p && p->n && version_cmp(p->v[p->n - 1].version, in[i].version) > 0;
			char *sv;

			if (!match(in[i].stem, argc, argv) || (upd && !newer))
				continue;
			if (tabs) {
				tab_line(in[i].stem, in[i].version, 1, newer, p && p->n ? &p->v[p->n - 1] : NULL);
				found = 1;
				continue;
			}
			sv = version_short(in[i].version);
			printf("%-36s %-22s i%c-\n", short_name(in[i].stem), sv, newer ? 'u' : '-');
			free(sv);
			found = 1;
		}
	}
	if (have_cat)
		catalog_free(&cat);
	installed_free(in, nin);
	if (!found && !upd) {
		if (argc || text)
			warnx("no packages match");
		return 1;
	}
	return 0;
}

/* --- manifests by name ------------------------------------------------------ */

/* the manifest of name[@version]: installed, or (remote) from the repository */
static int
get_manifest(const char *arg, int remote, struct manifest *m, int *installed)
{
	struct fmri f;
	struct installed *in;
	size_t nin, i;
	int r = -1;

	if (fmri_parse(arg, &f) < 0) {
		warnx("%s: not a package name", arg);
		return -1;
	}
	*installed = 0;
	installed_list(&im, &in, &nin);
	if (!remote) {
		for (i = 0; i < nin; i++)
			if (match(in[i].stem, 1, &f.stem)) {
				*m = in[i].m;
				memset(&in[i].m, 0, sizeof(in[i].m));
				*installed = 1;
				r = 0;
				break;
			}
		if (r < 0)
			warnx("%s: not installed (pkg info -r)", arg);
	} else {
		struct catalog cat;
		const struct catpkg *p = NULL;
		const struct catver *v = NULL;

		if (need_catalog(&cat) < 0)
			goto out;
		for (i = 0; i < cat.n; i++)
			if (match(cat.pkg[i].stem, 1, &f.stem)) {
				p = &cat.pkg[i];
				break;
			}
		if (p && p->n) {
			if (!f.version)
				v = &p->v[p->n - 1];
			else
				for (i = p->n; i-- > 0;)
					if (version_cmp(p->v[i].version, f.version) == 0 ||
					    !strncmp(p->v[i].version, f.version, strlen(f.version))) {
						v = &p->v[i];
						break;
					}
		}
		if (!v)
			warnx("%s: not in the catalog", arg);
		else {
			struct buf b = { 0 };

			if (remote_manifest(&im, p->stem, v->version, v->sig, &b) == 0 &&
			    manifest_parse(b.data, b.len, m) == 0) {
				r = 0;
				*installed = find_installed(in, nin, p->stem) &&
				    !strcmp(find_installed(in, nin, p->stem)->version, v->version);
			}
			buf_free(&b);
		}
		catalog_free(&cat);
	}
out:
	installed_free(in, nin);
	fmri_free(&f);
	return r;
}

static void
human(unsigned long long n, char *out, size_t sz)
{
	/* no %llu: AMIX's printf does not know long long */
	if (n < 1024)
		snprintf(out, sz, "%lu B", (unsigned long)n);
	else if (n < 1024 * 1024)
		snprintf(out, sz, "%.2f kB", n / 1024.0);
	else
		snprintf(out, sz, "%.2f MB", n / (1024.0 * 1024.0));
}

static int
cmd_info(int argc, char **argv)
{
	int remote = 0, c, rc = 0;
	char *arg;

	while ((c = subopt(argc, argv, "r", &arg)) != -1)
		remote = 1;
	if (optidx >= argc)
		usage();
	for (; optidx < argc; optidx++) {
		struct manifest m;
		int inst;
		const char *fv, *sum;
		struct fmri f;
		unsigned long long size = 0;
		unsigned long files = 0;
		size_t i;
		char hs[32];

		if (get_manifest(argv[optidx], remote, &m, &inst) < 0) {
			rc = 1;
			continue;
		}
		fv = manifest_set(&m, "pkg.fmri");
		if (!fv || fmri_parse(fv, &f) < 0) {
			warnx("%s: manifest without pkg.fmri", argv[optidx]);
			manifest_free(&m);
			rc = 1;
			continue;
		}
		for (i = 0; i < m.n; i++)
			if (!strcmp(m.act[i].type, "file")) {
				const char *s = action_get(&m.act[i], "pkg.size");

				files++;
				if (s)
					size += parse_ull(s);
			}
		sum = manifest_set(&m, "pkg.summary");
		human(size, hs, sizeof(hs));
		{
			char *ver = f.version ? f.version : "0";
			char *comma = strchr(ver, ','), *dash = strchr(ver, '-'), *colon = strchr(ver, ':');

			printf("          Name: %s\n", f.stem);
			printf("       Summary: %s\n", sum ? sum : "");
			printf("         State: %s\n", inst ? "Installed" : "Not installed");
			printf("     Publisher: %s\n", f.publisher ? f.publisher : PUBLISHER);
			printf("       Version: %.*s\n", (int)(comma ? comma - ver : (long)strlen(ver)), ver);
			if (comma)
				printf(" Build Release: %.*s\n",
				    (int)((dash ? dash : colon ? colon : ver + strlen(ver)) - comma - 1), comma + 1);
			if (dash)
				printf("        Branch: %.*s\n",
				    (int)((colon ? colon : ver + strlen(ver)) - dash - 1), dash + 1);
			if (colon)
				printf("Packaging Date: %s\n", colon + 1);
			printf("          Size: %s\n", hs);
			printf("         Files: %lu\n", files);
			printf("          FMRI: %s\n", fv);
		}
		if (optidx + 1 < argc)
			putchar('\n');
		fmri_free(&f);
		manifest_free(&m);
	}
	return rc;
}

static int
cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int
cmd_contents(int argc, char **argv)
{
	int remote = 0, raw = 0, c, inst;
	char *arg, *type = NULL;
	struct manifest m;
	struct buf b = { 0 };
	size_t i, n = 0;
	char **paths;

	while ((c = subopt(argc, argv, "rmt:", &arg)) != -1) {
		if (c == 'r')
			remote = 1;
		else if (c == 'm')
			raw = 1;
		else
			type = arg;
	}
	if (optidx + 1 != argc)
		usage();
	if (raw && remote) {
		/* the manifest as published, byte for byte */
		struct catalog cat;
		struct fmri f;
		const struct catpkg *p = NULL;

		if (fmri_parse(argv[optidx], &f) < 0 || need_catalog(&cat) < 0)
			return 1;
		for (i = 0; i < cat.n && !p; i++)
			if (match(cat.pkg[i].stem, 1, &f.stem))
				p = &cat.pkg[i];
		if (!p || !p->n) {
			warnx("%s: not in the catalog", argv[optidx]);
			return 1;
		}
		if (remote_manifest(&im, p->stem, p->v[p->n - 1].version, p->v[p->n - 1].sig, &b) < 0)
			return 1;
		fwrite(b.data, 1, b.len, stdout);
		buf_free(&b);
		catalog_free(&cat);
		fmri_free(&f);
		return 0;
	}
	if (get_manifest(argv[optidx], remote, &m, &inst) < 0)
		return 1;
	paths = xmalloc((m.n + 1) * sizeof(*paths));
	for (i = 0; i < m.n; i++) {
		const struct action *a = &m.act[i];
		const char *p = action_get(a, "path");

		if (raw) {
			size_t k;

			printf("%s%s%s", a->type, a->payload ? " " : "", a->payload ? a->payload : "");
			for (k = 0; k < a->nattr; k++)
				printf(strchr(a->attr[k].value, ' ') || !*a->attr[k].value ?
				    " %s=\"%s\"" : " %s=%s", a->attr[k].key, a->attr[k].value);
			putchar('\n');
			continue;
		}
		if (!p || (type && strcmp(type, a->type)))
			continue;
		paths[n++] = xasprintf("%s", p);
	}
	if (!raw) {
		qsort(paths, n, sizeof(*paths), cmp_str);
		printf("PATH\n");
		for (i = 0; i < n; i++)
			printf("%s\n", paths[i]);
	}
	for (i = 0; i < n; i++)
		free(paths[i]);
	free(paths);
	manifest_free(&m);
	return 0;
}

/* --- verify ----------------------------------------------------------------- */

/* uid/gid by name from the image's own etc/passwd and etc/group */
static long
lookup_id(const char *file, const char *name)
{
	char *f = root_path(im.root, file);
	struct buf b = { 0 };
	long id = -1;
	char *line, *nl;
	size_t nl_len = strlen(name);

	if (read_file(f, &b) == 0)
		for (line = b.data; line && *line; line = nl ? nl + 1 : NULL) {
			nl = strchr(line, '\n');
			if (!strncmp(line, name, nl_len) && line[nl_len] == ':') {
				char *p = strchr(line + nl_len + 1, ':');

				if (p)
					id = strtol(p + 1, NULL, 10);
				break;
			}
		}
	buf_free(&b);
	free(f);
	return id;
}

static int
check_attrs(const char *path, const struct action *a, const struct stat *st, int quiet,
    const char *stem, int *shown)
{
	const char *mode = action_get(a, "mode"), *owner = action_get(a, "owner");
	const char *group = action_get(a, "group");
	int bad = 0;

#define REPORT(...) do { \
		if (!quiet) { \
			if (!*shown) { printf("%s\n", stem); *shown = 1; } \
			printf("  %s: ", path); printf(__VA_ARGS__); putchar('\n'); \
		} bad = 1; } while (0)
	if (mode && (unsigned long)(st->st_mode & 07777) != strtoul(mode, NULL, 8))
		REPORT("mode %04lo, should be %s", (unsigned long)(st->st_mode & 07777), mode);
	if (owner) {
		long id = lookup_id("etc/passwd", owner);

		if (id < 0 || (long)st->st_uid != id)
			REPORT("owner %ld, should be %s", (long)st->st_uid, owner);
	}
	if (group) {
		long id = lookup_id("etc/group", group);

		if (id < 0 || (long)st->st_gid != id)
			REPORT("group %ld, should be %s", (long)st->st_gid, group);
	}
	return bad;
}

static int
verify_pkg(const struct installed *in, int quiet)
{
	size_t i;
	int bad = 0, shown_v = 0, *shown = &shown_v;
	const char *stem = in->stem, *path;

	for (i = 0; i < in->m.n; i++) {
		const struct action *a = &in->m.act[i];
		const char *p = action_get(a, "path"), *facet = action_get(a, "facet.devel");
		char *full;
		struct stat st;

		if (!p)
			continue;
		path = p;
		if (!im.devel && facet && !strcmp(facet, "true"))
			continue;		/* development files are not installed here */
		full = root_path(im.root, p);
		if (lstat(full, &st) < 0) {
			REPORT("missing");
			free(full);
			continue;
		}
		if (!strcmp(a->type, "dir")) {
			if (link_to_dir(full, &st))
				;		/* moved to another disk, e.g. /usr/local */
			else if (!S_ISDIR(st.st_mode))
				REPORT("not a directory");
			else
				bad |= check_attrs(p, a, &st, quiet, stem, shown);
		} else if (!strcmp(a->type, "file")) {
			const char *sz = action_get(a, "pkg.size"), *want = content_hash(a, "file");
			const char *preserve = action_get(a, "preserve");

			if (!S_ISREG(st.st_mode))
				REPORT("not a file");
			else {
				bad |= check_attrs(p, a, &st, quiet, stem, shown);
				if (!preserve) {	/* configuration may be changed */
					char h[65];

					if (sz && (unsigned long long)st.st_size != parse_ull(sz))
						REPORT("size %lu, should be %s", (unsigned long)st.st_size, sz);
					else if ((want || a->payload) &&
					    hash_file(full, want ? HASH_SHA512_256 : HASH_SHA1, h) < 0)
						REPORT("unreadable: %s", strerror(errno));
					else if (want) {
						if (strcmp(h, want))
							REPORT("content changed (sha512/256)");
					} else if (a->payload) {
						if (strcmp(h, a->payload))
							REPORT("content changed (sha1)");
					}
				}
			}
		} else if (!strcmp(a->type, "link")) {
			char t[4096];
			ssize_t n;
			const char *target = action_get(a, "target");

			if (!S_ISLNK(st.st_mode))
				REPORT("not a symbolic link");
			else if ((n = readlink(full, t, sizeof(t) - 1)) < 0 ||
			    (t[n] = 0, target && strcmp(t, target)))
				REPORT("points to %s, should be %s", n < 0 ? "?" : t, target ? target : "?");
		} else if (!strcmp(a->type, "hardlink")) {
			const char *target = action_get(a, "target");
			struct stat ts;
			char *tf;

			if (!target)
				continue;
			if (*target == '/')
				tf = root_path(im.root, target);
			else {
				char *dir = xstrdup(full), *sl = strrchr(dir, '/');

				if (sl)
					*sl = 0;
				tf = xasprintf("%s/%s", dir, target);
				free(dir);
			}
			if (stat(tf, &ts) < 0 || ts.st_ino != st.st_ino || ts.st_dev != st.st_dev)
				REPORT("not a hard link to %s", target);
			free(tf);
		}
		free(full);
	}
	return bad;
}

/* install, update, uninstall: options, then the names */
static int
cmd_change(int argc, char **argv, int what)
{
	int c;
	char *arg;

	while ((c = subopt(argc, argv, "n", &arg)) != -1)
		dry_run = 1;
#ifndef __amix__
	if (!strcmp(im.root, "/")) {
		warnx("pkg changes only AMIX itself; on another system use -R root");
		return 1;
	}
#endif
	argc -= optidx;
	argv += optidx;
	if (what == 2)
		return cmd_uninstall(&im, argc, argv);
	if (what == 0 && argc == 0)
		usage();
	return cmd_install(&im, argc, argv, what == 1);
}

static int
cmd_verify(int argc, char **argv)
{
	int quiet = 0, c, bad = 0, any = 0;
	char *arg;
	struct installed *in;
	size_t nin, i;

	while ((c = subopt(argc, argv, "q", &arg)) != -1)
		quiet = 1;
	argc -= optidx;
	argv += optidx;
	installed_list(&im, &in, &nin);
	for (i = 0; i < nin; i++) {
		if (!match(in[i].stem, argc, argv))
			continue;
		any = 1;
		bad |= verify_pkg(&in[i], quiet);
	}
	installed_free(in, nin);
	if (!any && argc) {
		warnx("no installed packages match");
		return 1;
	}
	return bad;
}

/*
 * pkg http get URL [FILE]   the body to FILE (or stdout); fails unless 200
 * pkg http put URL          stdin as the body; fails unless 2xx
 * For the AMIX Installer's bootstrap, which has no curl.
 */
static int
cmd_http(int argc, char **argv)
{
	struct buf in = { 0 }, out = { 0 };
	int status;

	if (argc < 3 || (strcmp(argv[1], "get") && strcmp(argv[1], "put")) ||
	    argc > (strcmp(argv[1], "get") ? 3 : 4))
		usage();
	if (!strcmp(argv[1], "put")) {
		char tmp[8192];
		size_t n;

		while ((n = fread(tmp, 1, sizeof(tmp), stdin)) > 0)
			buf_add(&in, tmp, n);
		status = http_request("PUT", argv[2], in.data, in.len, &out);
		buf_free(&in);
		buf_free(&out);
		if (status < 200 || status > 299) {
			warnx("%s: %s", argv[2], status < 0 ? strerror(errno) : "refused");
			return 1;
		}
		return 0;
	}
	status = http_request("GET", argv[2], NULL, 0, &out);
	if (status != 200) {
		if (status < 0)
			warnx("%s: %s", argv[2], strerror(errno));
		else
			warnx("%s: HTTP %d", argv[2], status);
		buf_free(&out);
		return 1;
	}
	if (argc == 4) {
		if (write_atomic(argv[3], out.data, out.len) < 0) {
			warnx("%s: %s", argv[3], strerror(errno));
			buf_free(&out);
			return 1;
		}
	} else
		fwrite(out.data, 1, out.len, stdout);
	buf_free(&out);
	return 0;
}

int
main(int argc, char **argv)
{
	const char *root = getenv("PKG_IMAGE");
	int i;

	self_argv = argv;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-R") && i + 1 < argc)
			root = argv[++i];
		else if (!strncmp(argv[i], "-R", 2) && argv[i][2])
			root = argv[i] + 2;
		else if (!strcmp(argv[i], "-v"))
			verbose = 1;
		else if (!strcmp(argv[i], "--min-free") && i + 1 < argc)
			min_free_mb = strtoul(argv[++i], NULL, 10);
		else
			usage();
	}
	if (i >= argc)
		usage();
	if (!strcmp(argv[i], "http"))
		return cmd_http(argc - i, argv + i);
	image_open(&im, root);
	argc -= i;
	argv += i;
	if (!strcmp(argv[0], "set-publisher"))
		return cmd_set_publisher(argc, argv);
	if (!strcmp(argv[0], "unset-publisher"))
		return cmd_unset_publisher(argc, argv);
	if (!strcmp(argv[0], "publisher"))
		return cmd_publisher(argc, argv);
	if (!strcmp(argv[0], "search"))
		return cmd_search(argc, argv);
	if (!strcmp(argv[0], "refresh"))
		return cmd_refresh();
	if (!strcmp(argv[0], "list"))
		return cmd_list(argc, argv);
	if (!strcmp(argv[0], "info"))
		return cmd_info(argc, argv);
	if (!strcmp(argv[0], "contents"))
		return cmd_contents(argc, argv);
	if (!strcmp(argv[0], "verify"))
		return cmd_verify(argc, argv);
	if (!strcmp(argv[0], "install"))
		return cmd_change(argc, argv, 0);
	if (!strcmp(argv[0], "update"))
		return cmd_change(argc, argv, 1);
	if (!strcmp(argv[0], "uninstall"))
		return cmd_change(argc, argv, 2);
	if (!strcmp(argv[0], "history"))
		return cmd_history(&im);
	if (!strcmp(argv[0], "version")) {
		printf("%s\n", VERSION_STRING);
		return 0;
	}
	usage();
	return 2;
}
