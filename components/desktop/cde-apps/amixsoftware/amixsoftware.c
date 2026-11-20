/*
 * amixsoftware.c -- the AMIX Software Center: one CDE window over both
 * package systems of AMIX, as equals:
 *
 *   IPS   our pkg (sysutils/amix-pkg): publisher amix, its origins, the
 *         packages of amix-userland; read with "pkg list -at" (no root);
 *   apkg  the package client of the amigaux project (pkg.amigaux.org): SVR4
 *         packages through pkgadd/pkgrm; its catalog (/var/apkg/catalog, or
 *         beside cachedir= in /etc/apkg.conf) and /var/sadm/pkg read here.
 *
 * One list shows both, each row with its source.  Categories on the left
 * (All, Installed, Updates, In both, then the packages' own), the packages
 * in the middle, the chosen one's details on the right with one clear
 * button: Install, Update or Remove, and "Try" beside it (the system's own
 * dry run: pkg -n, apkg -n).  Each change goes to the system that owns the
 * package.  Software both systems offer (an IPS editor/vim and an apkg vim)
 * is paired: "In both" shows the pairs, and installing one while the other
 * is installed asks first and names what overlaps.
 *
 * Changes run as a queue in the progress window, one after the other (both
 * systems write /var/sadm/install/contents: never at the same time).  root
 * runs the programs directly; anyone else gives root's password once per
 * change to the setuid helper swrun, which runs only these programs with a
 * fixed set of verbs.
 *
 * Repositories: IPS's origins (pkg publisher/set-publisher, through swrun)
 * and apkg's repo=, cachedir=, nameserver= in /etc/apkg.conf; Find on
 * Network asks every AMIX Installer that serves packages (UDP broadcast on
 * port 8070, and 10.0.2.2 for FS-UAE) for its repository.
 *
 * Preferences: both systems on or off with their programs' paths, which one
 * installs when both offer the software, the space to keep free, dry runs,
 * 68060-only packages, removing original Amix 2.1 packages.  Saved for
 * everyone (/etc/amixsoftware.conf, through swrun) or for the user
 * (~/.amixsoftware, which overrides the system's), as amixdisplay does.
 *
 *   amixsoftware          the window
 *   amixsoftware -list    the merged table on stdout, no window (tests)
 *
 * AMIX traps kept in mind: no strtok_r; no wildcard font patterns (a scaled
 * instance renders far slower); recomputeSize False on labels that change.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <Xm/Xm.h>
#include <Xm/MainW.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/TextF.h>
#include <Xm/Text.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>
#include <Xm/MessageB.h>
#include <Xm/FileSB.h>
#include <Xm/DrawingA.h>
#include <Xm/SelectioB.h>

#define TITLE      "AMIX Software Center"
#define SWRUN      "/opt/amix/lib/amixsoftware/swrun"	/* AMIXSOFTWARE_SWRUN for tests */
#define DEF_APKG   "/usr/apkg/bin/apkg"
#define DEF_PKG    "/usr/bin/pkg"
#define APKG_CONF  "/etc/apkg.conf"	/* AMIXSOFTWARE_APKGCONF for tests */
#define SYS_CONF   "/etc/amixsoftware.conf"	/* AMIXSOFTWARE_SYSCONF for tests */
#define DEF_CATDIR "/var/apkg"
#define SADM       "/var/sadm/pkg"	/* AMIXSOFTWARE_SADM for tests */
#define CONTENTS   "/var/sadm/install/contents"
#define IPS_ATTRS  "/var/pkg/publisher/amix/catalog/catalog.attrs"
#define DISCOVERY_PORT 8070
#define MAXPKG     1500
#define MAXCAT     64
#define MAXJOB     40
#define MAXARG     48

enum { S_IPS, S_APKG };
static const char *src_name[] = { "IPS", "apkg" };

struct pkg {
	int src;
	char name[64];			/* apkg name, or IPS stem (editor/vim) */
	char base[40];			/* lower-case name without category: pairs the two */
	char version[40];		/* the newest available ("" when not in a catalog) */
	char inst[40];			/* apkg: the SVR4 package instance */
	char file[120];
	long size;			/* apkg: download bytes; IPS: -1 (pkg -n tells) */
	char deps[256];			/* "a,b" or "" */
	char cat[32];
	char desc[256];
	char cpu[8];			/* IPS: 68020, 68060 or "" */
	int installed;
	int original;			/* apkg: installed without VERSION, Amix 2.1 */
	char iver[40];			/* the installed version */
	int update;			/* a newer one is there */
	int twin;			/* the same software in the other system, or -1 */
};

static struct pkg pk[MAXPKG];
static int npk;
static char cats[MAXCAT][32];
static int ncats;
static int view[MAXPKG], nview;		/* list position -> pk index */
static int sel = -1;			/* pk index shown on the right */
static int cur_cat;			/* 0 All, 1 Installed, 2 Updates, 3 In both, 4+ cats[i-4] */
static int src_filter = -1;		/* -1 both, S_IPS, S_APKG */
#define NFIXED 4

/* settings: the defaults, then /etc/amixsoftware.conf, then ~/.amixsoftware */
struct settings {
	int ips_on, apkg_on;
	char ips_path[256], apkg_path[256];
	int prefer;			/* S_IPS or S_APKG when both offer it */
	int show_both;
	int min_free;			/* MB */
	int confirm, dryrun, show060, ask_original, update_at_start;
};
static struct settings cfg, syscfg;
static int user_file;			/* ~/.amixsoftware exists */

/* the programs, as found */
static int ips_ok, apkg_ok, has060;
static char ips_ver[64], apkg_ver[64], ips_note[200];
static char repo[256] = "(not set)", catalog[320] = DEF_CATDIR "/catalog";
static char apkg_cachedir[256] = "", apkg_ns[64] = "";
static time_t apkg_time, ips_time;

static XtAppContext app;
static Widget toolbar, searchlabel, srcmenu;
static Widget top, warnbar, warnlabel, l_cats, l_pkgs, t_search, l_status;
static Widget d_name, d_sub, d_desc, d_status, d_size, d_needs, d_neededby, d_twin, b_main, b_try;
static Widget tb_check, tb_updall, tb_repos;
static Widget m_install, m_upgrade, m_remove, m_try, m_check, m_updall;
static XFontStruct *mono;
static Pixel bar_pixel;			/* the progress bar's fill */

/* ------------------------------------------------------------ helpers */

static void set_label(Widget w, const char *s)
{
	XmString x = XmStringCreateLtoR((char *)s, XmFONTLIST_DEFAULT_TAG);

	XtVaSetValues(w, XmNlabelString, x, NULL);
	XmStringFree(x);
}

static void status(const char *s)
{
	set_label(l_status, s);
}

static void destroy_cb(Widget w, XtPointer cd, XtPointer cb)
{
	XtDestroyWidget(XtParent(w));
}

static Widget message(const char *text, int kind)
{
	Widget d;
	XmString x = XmStringCreateLtoR((char *)text, XmFONTLIST_DEFAULT_TAG);

	if (kind == 2)
		d = XmCreateErrorDialog(top, "error", NULL, 0);
	else if (kind == 1)
		d = XmCreateWarningDialog(top, "warning", NULL, 0);
	else
		d = XmCreateInformationDialog(top, "info", NULL, 0);
	XtVaSetValues(d, XmNmessageString, x, NULL);
	XtVaSetValues(XtParent(d), XmNtitle, TITLE, NULL);
	XmStringFree(x);
	XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
	XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
	XtAddCallback(d, XmNokCallback, destroy_cb, NULL);
	XtManageChild(d);
	return d;
}

static void human(long bytes, char *out)
{
	if (bytes < 0)
		strcpy(out, "-");
	else if (bytes >= 1024L * 1024)
		sprintf(out, "%ld.%ld MB", bytes / (1024L * 1024), (bytes % (1024L * 1024)) * 10 / (1024L * 1024));
	else if (bytes >= 1024)
		sprintf(out, "%ld KB", (bytes + 512) / 1024);
	else
		sprintf(out, "%ld bytes", bytes);
}

static long free_kb(const char *path)
{
	struct statvfs v;

	if (statvfs(path, &v) < 0)
		return -1;
	return (long)((double)v.f_bavail * (v.f_frsize ? v.f_frsize : v.f_bsize) / 1024);
}

static void trim(char *s)
{
	int n = strlen(s);

	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
		s[--n] = 0;
}

static void copy(char *dst, const char *src, int size)
{
	strncpy(dst, src, size - 1);
	dst[size - 1] = 0;
}

static void lower(char *s)
{
	for (; *s; s++)
		*s = tolower((unsigned char)*s);
}

/* compare versions piece by piece: numbers as numbers, the rest as text */
static int vercmp(const char *a, const char *b)
{
	while (*a || *b) {
		if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
			long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);

			if (x != y)
				return x < y ? -1 : 1;
		} else {
			if (*a != *b)
				return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
			a++, b++;
		}
	}
	return 0;
}

static const char *env_or(const char *name, const char *def)
{
	const char *e = getenv(name);

	return e && *e ? e : def;
}

/* a 68060 here: the port kernel names it in uname's machine string */
static int cpu_has_060(void)
{
	const char *e = getenv("AMIXSOFTWARE_CPU");
	struct utsname u;

	if (e && *e)
		return strstr(e, "60") != NULL;
	return uname(&u) == 0 && strstr(u.machine, "68060") != NULL;
}

/*
 * run argv, its stdout into out (at most max-1 bytes, NUL-ended); stderr
 * into err when err is given.  Returns the exit status, or -1.
 */
static int capture(char **argv, char *out, int max, char *err, int emax)
{
	int p[2], e[2] = { -1, -1 }, n = 0, en = 0, r, st;
	pid_t pid;

	out[0] = 0;
	if (err)
		err[0] = 0;
	if (pipe(p) < 0 || (err && pipe(e) < 0))
		return -1;
	if ((pid = fork()) == 0) {
		int nul = open("/dev/null", O_RDWR);

		dup2(nul, 0);
		dup2(p[1], 1);
		dup2(err ? e[1] : nul, 2);
		close(p[0]);
		if (err)
			close(e[0]);
		execv(argv[0], argv);
		_exit(127);
	}
	close(p[1]);
	if (err)
		close(e[1]);
	if (pid < 0) {
		close(p[0]);
		if (err)
			close(e[0]);
		return -1;
	}
	/* stdout first, then stderr: pkg writes little to stderr */
	while (n < max - 1 && (r = read(p[0], out + n, max - 1 - n)) > 0)
		n += r;
	if (n >= max - 1) {		/* full: drain the rest, or the child blocks */
		char junk[512];

		while (read(p[0], junk, sizeof junk) > 0)
			;
	}
	out[n] = 0;
	close(p[0]);
	if (err) {
		while (en < emax - 1 && (r = read(e[0], err + en, emax - 1 - en)) > 0)
			en += r;
		err[en] = 0;
		close(e[0]);
	}
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* the next tab-separated field of *s into out (AMIX has no strtok_r) */
static char *next_field(char **s)
{
	char *f = *s, *t;

	if (!f)
		return "";
	if ((t = strchr(f, '\t'))) {
		*t = 0;
		*s = t + 1;
	} else
		*s = NULL;
	return f;
}

/* ------------------------------------------------------------ settings */

static void defaults(struct settings *s)
{
	memset(s, 0, sizeof *s);
	s->ips_on = s->apkg_on = 1;
	strcpy(s->ips_path, DEF_PKG);
	strcpy(s->apkg_path, DEF_APKG);
	s->prefer = S_IPS;
	s->show_both = 1;
	s->min_free = 100;
	s->confirm = 1;
	s->ask_original = 1;
}

static char *user_settings(void)
{
	static char path[300];

	sprintf(path, "%s/.amixsoftware", env_or("HOME", "/"));
	return path;
}

static int read_settings(const char *path, struct settings *s)
{
	FILE *f = fopen(path, "r");
	char line[300];

	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		char *v = strchr(line, '=');

		trim(line);
		if (line[0] == '#' || !v)
			continue;
		*v++ = 0;
		if (!strcmp(line, "ips_on"))
			s->ips_on = atoi(v) != 0;
		else if (!strcmp(line, "apkg_on"))
			s->apkg_on = atoi(v) != 0;
		else if (!strcmp(line, "ips_path") && *v)
			copy(s->ips_path, v, sizeof s->ips_path);
		else if (!strcmp(line, "apkg") && *v)	/* the key of 2026-11-10 */
			copy(s->apkg_path, v, sizeof s->apkg_path);
		else if (!strcmp(line, "prefer"))
			s->prefer = !strcmp(v, "apkg") ? S_APKG : S_IPS;
		else if (!strcmp(line, "show_both"))
			s->show_both = atoi(v) != 0;
		else if (!strcmp(line, "min_free"))
			s->min_free = atoi(v) > 0 ? atoi(v) : 100;
		else if (!strcmp(line, "confirm"))
			s->confirm = atoi(v) != 0;
		else if (!strcmp(line, "dryrun"))
			s->dryrun = atoi(v) != 0;
		else if (!strcmp(line, "show060"))
			s->show060 = atoi(v) != 0;
		else if (!strcmp(line, "ask_original"))
			s->ask_original = atoi(v) != 0;
		else if (!strcmp(line, "update_at_start"))
			s->update_at_start = atoi(v) != 0;
	}
	fclose(f);
	return 0;
}

static void settings_text(const struct settings *s, char *out)
{
	sprintf(out, "ips_on=%d\nips_path=%s\napkg_on=%d\napkg=%s\nprefer=%s\nshow_both=%d\n"
		"min_free=%d\nconfirm=%d\ndryrun=%d\nshow060=%d\nask_original=%d\nupdate_at_start=%d\n",
		s->ips_on, s->ips_path, s->apkg_on, s->apkg_path, s->prefer == S_APKG ? "apkg" : "ips",
		s->show_both, s->min_free, s->confirm, s->dryrun, s->show060, s->ask_original,
		s->update_at_start);
}

static void load_settings(void)
{
	defaults(&syscfg);
	read_settings(env_or("AMIXSOFTWARE_SYSCONF", SYS_CONF), &syscfg);
	cfg = syscfg;
	user_file = read_settings(user_settings(), &cfg) == 0;
}

static int save_user_settings(void)
{
	char tmp[310], text[2048];
	FILE *f;

	sprintf(tmp, "%s.new", user_settings());
	if (!(f = fopen(tmp, "w")))
		return -1;
	settings_text(&cfg, text);
	fprintf(f, "# AMIX Software Center: your settings (everyone's: %s)\n%s", SYS_CONF, text);
	if (fclose(f) != 0)
		return -1;
	user_file = 1;
	return rename(tmp, user_settings());
}

/* ------------------------------------------------------------ the programs */

/* is it there, and which version: "PATH version" prints it ("apkg 0.2.3" or "0.5.0") */
static int check_program(const char *path, char *ver, const char *prefix)
{
	struct stat st;
	char out[128], *argv[3];

	ver[0] = 0;
	if (path[0] != '/' || stat(path, &st) < 0 || !S_ISREG(st.st_mode) || access(path, X_OK) < 0)
		return 0;
	argv[0] = (char *)path;
	argv[1] = "version";
	argv[2] = NULL;
	if (capture(argv, out, sizeof out, NULL, 0) != 0)
		return 0;
	trim(out);
	if (prefix && strncmp(out, prefix, strlen(prefix)))
		return 0;
	copy(ver, out + (prefix ? strlen(prefix) : 0), 64);
	return 1;
}

static void check_programs(void)
{
	ips_ok = cfg.ips_on && check_program(cfg.ips_path, ips_ver, NULL);
	apkg_ok = cfg.apkg_on && check_program(cfg.apkg_path, apkg_ver, "apkg ");
}

/* ------------------------------------------------------------ apkg data */

static const char *apkg_conf(void)
{
	return env_or("AMIXSOFTWARE_APKGCONF", APKG_CONF);
}

static const char *sadm(void)
{
	return env_or("AMIXSOFTWARE_SADM", SADM);
}

static void load_apkg_conf(void)
{
	FILE *f = fopen(apkg_conf(), "r");
	char line[300], dir[300];

	strcpy(dir, DEF_CATDIR "/cache");
	strcpy(repo, "(not set)");
	apkg_cachedir[0] = apkg_ns[0] = 0;
	if (f) {
		while (fgets(line, sizeof line, f)) {
			trim(line);
			if (!strncmp(line, "repo=", 5))
				copy(repo, line + 5, sizeof repo);
			else if (!strncmp(line, "cachedir=", 9) && line[9]) {
				copy(dir, line + 9, sizeof dir);
				copy(apkg_cachedir, line + 9, sizeof apkg_cachedir);
			} else if (!strncmp(line, "nameserver=", 11))
				copy(apkg_ns, line + 11, sizeof apkg_ns);
		}
		fclose(f);
	}
	{
		char *s = strrchr(dir, '/');

		if (s && s != dir)
			*s = 0;
		snprintf(catalog, sizeof catalog, "%s/catalog", dir);
	}
}

static int split(char *s, char **f, int max)
{
	int n = 0;

	f[n++] = s;
	while (*s && n < max) {
		if (*s == '|') {
			*s = 0;
			f[n++] = s + 1;
		}
		s++;
	}
	return n;
}

static void apkg_installed(struct pkg *p)
{
	char path[200], line[200];
	FILE *f;

	p->installed = p->original = p->update = 0;
	p->iver[0] = 0;
	sprintf(path, "%s/%s/pkginfo", sadm(), p->inst);
	if (!(f = fopen(path, "r")))
		return;
	p->installed = 1;
	while (fgets(line, sizeof line, f)) {
		trim(line);
		if (!strncmp(line, "VERSION=", 8))
			copy(p->iver, line + 8, sizeof p->iver);
	}
	fclose(f);
	if (!p->iver[0])
		p->original = 1;
	else if (vercmp(p->iver, p->version) < 0)
		p->update = 1;
}

/* 0, -1 no catalog, -2 not an apkg catalog */
static int load_apkg(void)
{
	FILE *f = fopen(catalog, "r");
	char line[1200], *fl[10];
	struct stat st;
	int first = 1;

	apkg_time = 0;
	if (!f)
		return -1;
	if (fstat(fileno(f), &st) == 0)
		apkg_time = st.st_mtime;
	while (fgets(line, sizeof line, f) && npk < MAXPKG) {
		struct pkg *p = &pk[npk];

		trim(line);
		if (first) {
			first = 0;
			if (strcmp(line, "#apkg-catalog 1")) {
				fclose(f);
				return -2;
			}
			continue;
		}
		if (!line[0] || line[0] == '#')
			continue;
		if (split(line, fl, 10) < 9)
			continue;
		memset(p, 0, sizeof *p);
		p->src = S_APKG;
		p->twin = -1;
		copy(p->name, fl[0], sizeof p->name);
		copy(p->version, fl[1], sizeof p->version);
		copy(p->inst, fl[2], sizeof p->inst);
		copy(p->file, fl[3], sizeof p->file);
		p->size = atol(fl[5]);
		copy(p->deps, strcmp(fl[6], "none") ? fl[6] : "", sizeof p->deps);
		copy(p->cat, fl[7], sizeof p->cat);
		lower(p->cat);
		copy(p->desc, fl[8], sizeof p->desc);
		copy(p->base, p->name, sizeof p->base);
		lower(p->base);
		apkg_installed(p);
		npk++;
	}
	fclose(f);
	return 0;
}

/* ------------------------------------------------------------ IPS data */

/* one line of "pkg list -t": stem, version, flags, cpu, newest, requires, summary */
static void ips_line(char *line)
{
	struct pkg *p = &pk[npk];
	char *s = line, *stem, *ver, *flags, *cpu, *newest, *deps, *sum, *slash;

	if (npk >= MAXPKG || !*line)
		return;
	stem = next_field(&s);
	ver = next_field(&s);
	flags = next_field(&s);
	cpu = next_field(&s);
	newest = next_field(&s);
	deps = next_field(&s);
	sum = next_field(&s);
	if (!*stem || !*ver || strlen(flags) < 2)
		return;
	memset(p, 0, sizeof *p);
	p->src = S_IPS;
	p->twin = -1;
	p->size = -1;
	copy(p->name, stem, sizeof p->name);
	p->installed = flags[0] == 'i';
	p->update = flags[1] == 'u';
	if (p->installed)
		copy(p->iver, ver, sizeof p->iver);
	copy(p->version, strcmp(newest, "-") ? newest : ver, sizeof p->version);
	copy(p->cpu, strcmp(cpu, "-") ? cpu : "", sizeof p->cpu);
	copy(p->deps, strcmp(deps, "-") ? deps : "", sizeof p->deps);
	copy(p->desc, sum, sizeof p->desc);
	slash = strchr(stem, '/');
	if (slash) {
		int n = slash - stem;

		copy(p->cat, stem, n + 1 < (int)sizeof p->cat ? n + 1 : (int)sizeof p->cat);
	} else
		strcpy(p->cat, "other");
	copy(p->base, strrchr(stem, '/') ? strrchr(stem, '/') + 1 : stem, sizeof p->base);
	lower(p->base);
	npk++;
}

/* 0; -1 no catalog yet (installed ones only) */
static int load_ips(void)
{
	static char out[262144];
	char err[512], *argv[4], *line, *nl;
	struct stat st;
	int r;

	ips_note[0] = 0;
	ips_time = stat(IPS_ATTRS, &st) == 0 ? st.st_mtime : 0;
	argv[0] = cfg.ips_path;
	argv[1] = "list";
	argv[2] = "-at";
	argv[3] = NULL;
	r = capture(argv, out, sizeof out, err, sizeof err);
	if (r != 0) {
		/* no catalog yet: what is installed, at least */
		argv[2] = "-t";
		if (capture(argv, out, sizeof out, NULL, 0) != 0)
			out[0] = 0;
		strcpy(ips_note, strstr(err, "no catalog") ? "no catalog yet: Check for Updates"
							   : "pkg list failed");
		r = -1;
	}
	for (line = out; line && *line; line = nl) {
		nl = strchr(line, '\n');
		if (nl)
			*nl++ = 0;
		ips_line(line);
	}
	return r;
}

/* ------------------------------------------------------------ both */

static int cat_index(const char *c)
{
	int i;

	for (i = 0; i < ncats; i++)
		if (!strcmp(cats[i], c))
			return i;
	if (ncats < MAXCAT) {
		copy(cats[ncats], c, sizeof cats[0]);
		return ncats++;
	}
	return -1;
}

static int cmp_cat(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* the same software in both: an IPS package and an apkg one of the same name */
static void pair_twins(void)
{
	int i, k;

	for (i = 0; i < npk; i++)
		if (pk[i].src == S_IPS)
			for (k = 0; k < npk; k++)
				if (pk[k].src == S_APKG && pk[k].twin < 0 && !strcmp(pk[i].base, pk[k].base)) {
					pk[i].twin = k;
					pk[k].twin = i;
					break;
				}
}

/* hidden: a 68060-only package on a machine without one, unless asked for */
static int hidden(const struct pkg *p)
{
	return !cfg.show060 && !has060 && !strcmp(p->cpu, "68060") && !p->installed;
}

/* runs on a 68060 only, and this machine has none */
static int cpu_bad(const struct pkg *p)
{
	return !has060 && !strcmp(p->cpu, "68060");
}

static int ips_state = 0, apkg_state = 0;	/* load results: 0 ok, -1 no catalog, -2 bad */

static void load_all(void)
{
	int i;

	npk = ncats = 0;
	check_programs();
	load_apkg_conf();
	ips_state = ips_ok ? load_ips() : 0;
	apkg_state = cfg.apkg_on ? load_apkg() : 0;
	pair_twins();
	for (i = 0; i < npk; i++)
		cat_index(pk[i].cat);
	qsort(cats, ncats, sizeof cats[0], cmp_cat);
}

static int find_pkg(int src, const char *name)
{
	int i;

	for (i = 0; i < npk; i++)
		if (pk[i].src == src && !strcmp(pk[i].name, name))
			return i;
	return -1;
}

/* the next name of a "a,b,c" list into out (AMIX has no strtok_r) */
static int next_dep(const char **s, char *out, int size)
{
	int n = 0;

	while (**s == ',' || **s == ' ')
		(*s)++;
	if (!**s)
		return 0;
	while (**s && **s != ',') {
		if (n < size - 1)
			out[n++] = **s;
		(*s)++;
	}
	out[n] = 0;
	return 1;
}

/* the packages an install of p brings, p last; deps first, each once */
static void resolve(int p, int *out, int *n, int depth)
{
	const char *l = pk[p].deps;
	char d[64];
	int i;

	if (depth > 20 || *n >= 60)
		return;
	for (i = 0; i < *n; i++)
		if (out[i] == p)
			return;
	while (next_dep(&l, d, sizeof d)) {
		int q = find_pkg(pk[p].src, d);

		if (q >= 0 && !pk[q].installed)
			resolve(q, out, n, depth + 1);
	}
	for (i = 0; i < *n; i++)
		if (out[i] == p)
			return;
	out[(*n)++] = p;
}

static int has_dep(const struct pkg *p, const char *name)
{
	const char *l = p->deps;
	char d[64];

	while (next_dep(&l, d, sizeof d))
		if (!strcmp(d, name))
			return 1;
	return 0;
}

/* -list: the merged table, no window */
static int list_main(void)
{
	int i;

	has060 = cpu_has_060();
	load_all();
	printf("# IPS %s%s%s; apkg %s%s; 68060 %s\n", ips_ok ? "pkg " : "off/not found", ips_ok ? ips_ver : "",
	       ips_note[0] ? " (no catalog)" : "", apkg_ok ? "found " : "off/not found", apkg_ok ? apkg_ver : "",
	       has060 ? "yes" : "no");
	for (i = 0; i < npk; i++) {
		struct pkg *p = &pk[i];

		printf("%s\t%s\t%s\t%s\t%s\t%c%c%c\t%s\t%s\t%s\n", src_name[p->src], p->name, p->cat,
		       p->version, p->iver[0] ? p->iver : "-", p->installed ? 'i' : '-', p->update ? 'u' : '-',
		       p->original ? 'o' : '-', p->cpu[0] ? p->cpu : "-",
		       p->twin >= 0 ? pk[p->twin].name : "-", hidden(p) ? "hidden" : "shown");
	}
	return 0;
}

/* ------------------------------------------------------------ the lists */

static int matches(const struct pkg *p, const char *q)
{
	char a[340], b[64];
	int i;

	if (!*q)
		return 1;
	for (i = 0; q[i] && i < 63; i++)
		b[i] = tolower((unsigned char)q[i]);
	b[i] = 0;
	snprintf(a, sizeof a, "%s %s", p->name, p->desc);
	for (i = 0; a[i]; i++)
		a[i] = tolower((unsigned char)a[i]);
	return strstr(a, b) != NULL;
}

static void state_word(const struct pkg *p, char *out)
{
	if (p->update)
		strcpy(out, "update");
	else if (p->original)
		strcpy(out, "Amix 2.1");
	else if (p->installed)
		strcpy(out, "installed");
	else if (cpu_bad(p))
		strcpy(out, "68060 only");
	else
		out[0] = 0;
}

/* shown at all: the source filter, a system that is off, 68060-only, the
 * preferred one only when show_both is off */
static int visible(const struct pkg *p)
{
	if (src_filter >= 0 && p->src != src_filter)
		return 0;
	if (hidden(p))
		return 0;
	if (!cfg.show_both && p->twin >= 0 && p->src != cfg.prefer && !p->installed && !pk[p->twin].installed)
		return 0;
	return 1;
}

static void fill_cats(void)
{
	XmString *it = (XmString *)XtMalloc((ncats + NFIXED) * sizeof(XmString));
	int i, j, nall = 0, ninst = 0, nupd = 0, nboth = 0, n;
	char s[80];

	for (i = 0; i < npk; i++) {
		if (!visible(&pk[i]))
			continue;
		nall++;
		ninst += pk[i].installed;
		nupd += pk[i].update;
		nboth += pk[i].twin >= 0;
	}
	sprintf(s, "All  (%d)", nall);
	it[0] = XmStringCreateLocalized(s);
	sprintf(s, "Installed  (%d)", ninst);
	it[1] = XmStringCreateLocalized(s);
	sprintf(s, "Updates  (%d)", nupd);
	it[2] = XmStringCreateLocalized(s);
	sprintf(s, "In both  (%d)", nboth);
	it[3] = XmStringCreateLocalized(s);
	for (i = 0; i < ncats; i++) {
		for (n = j = 0; j < npk; j++)
			n += visible(&pk[j]) && !strcmp(pk[j].cat, cats[i]);
		sprintf(s, "%c%s  (%d)", toupper((unsigned char)cats[i][0]), cats[i] + 1, n);
		it[i + NFIXED] = XmStringCreateLocalized(s);
	}
	XmListDeleteAllItems(l_cats);
	XmListAddItems(l_cats, it, ncats + NFIXED, 0);
	for (i = 0; i < ncats + NFIXED; i++)
		XmStringFree(it[i]);
	XtFree((char *)it);
	if (cur_cat >= ncats + NFIXED)
		cur_cat = 0;
	XmListSelectPos(l_cats, cur_cat + 1, False);
}

static void show_details(void);

static void fill_pkgs(void)
{
	char *q = XmTextFieldGetString(t_search);
	XmString *it = (XmString *)XtMalloc((npk + 1) * sizeof(XmString));
	int i, keep = -1;
	char row[400], st[60];

	nview = 0;
	for (i = 0; i < npk; i++) {
		struct pkg *p = &pk[i];

		if (!visible(p))
			continue;
		if (cur_cat == 1 && !p->installed)
			continue;
		if (cur_cat == 2 && !p->update)
			continue;
		if (cur_cat == 3 && p->twin < 0)
			continue;
		if (cur_cat >= NFIXED && strcmp(p->cat, cats[cur_cat - NFIXED]))
			continue;
		if (!matches(p, q))
			continue;
		state_word(p, st);
		sprintf(row, " %c %-22.22s %-4s %-11.11s %-10.10s %-.30s", p->twin >= 0 ? '!' : ' ', p->name,
			src_name[p->src], p->installed ? p->iver : p->version, st, p->desc);
		if (i == sel)
			keep = nview;
		view[nview] = i;
		it[nview++] = XmStringCreateLocalized(row);
	}
	XtFree(q);
	XmListDeleteAllItems(l_pkgs);
	if (nview)
		XmListAddItems(l_pkgs, it, nview, 0);
	for (i = 0; i < nview; i++)
		XmStringFree(it[i]);
	XtFree((char *)it);
	if (keep < 0 && nview)
		keep = 0;
	sel = keep >= 0 ? view[keep] : -1;
	if (keep >= 0) {
		XmListSelectPos(l_pkgs, keep + 1, False);
		XmListSetKbdItemPos(l_pkgs, keep + 1);
	}
	show_details();
}

static void refresh_status(void)
{
	char s[800], a[300], b[300], when[40];
	int ninst = 0, i;

	for (i = 0; i < npk; i++)
		ninst += pk[i].installed;
	if (!cfg.ips_on)
		strcpy(a, "IPS off");
	else if (!ips_ok)
		strcpy(a, "IPS pkg not found");
	else if (ips_note[0])
		sprintf(a, "IPS pkg %s: %s", ips_ver, ips_note);
	else {
		strcpy(when, "?");
		if (ips_time)
			strftime(when, sizeof when, "%d %b %H:%M", localtime(&ips_time));
		sprintf(a, "IPS pkg %s, catalog %s", ips_ver, when);
	}
	if (!cfg.apkg_on)
		strcpy(b, "apkg off");
	else if (!apkg_ok)
		strcpy(b, "apkg not found");
	else {
		strcpy(when, "never");
		if (apkg_time)
			strftime(when, sizeof when, "%d %b %H:%M", localtime(&apkg_time));
		sprintf(b, "apkg %s, catalog %s", apkg_ver, when);
	}
	sprintf(s, "%d packages, %d installed.    %s.    %s.    %s, / %ld MB free", npk, ninst, a, b,
		has060 ? "68060" : "no 68060", free_kb("/") / 1024);
	status(s);
}

/* ------------------------------------------------------------ actions */

enum act { A_NONE, A_INSTALL, A_UPGRADE, A_REMOVE };

static int busy(void);

static int src_ok(int src)
{
	return src == S_IPS ? ips_ok : apkg_ok;
}

static enum act main_action(int p)
{
	if (p < 0)
		return A_NONE;
	if (pk[p].update)
		return A_UPGRADE;
	if (pk[p].installed)
		return A_REMOVE;
	return A_INSTALL;
}

static int can(enum act a, int p)
{
	if (p < 0 || busy() || !src_ok(pk[p].src))
		return 0;
	if (a == A_INSTALL)
		return !pk[p].installed && !cpu_bad(&pk[p]) && pk[p].version[0];
	if (a == A_UPGRADE)
		return pk[p].update && !cpu_bad(&pk[p]);
	if (a == A_REMOVE)
		return pk[p].installed;
	return 0;
}

static void set_sensitivity(void)
{
	int idle = !busy();
	enum act a = main_action(sel);
	int any_upd = 0, i;

	for (i = 0; i < npk; i++)
		any_upd |= pk[i].update && src_ok(pk[i].src) && !cpu_bad(&pk[i]);
	XtSetSensitive(tb_check, idle && (ips_ok || apkg_ok));
	XtSetSensitive(m_check, idle && (ips_ok || apkg_ok));
	XtSetSensitive(tb_updall, idle && any_upd);
	XtSetSensitive(m_updall, idle && any_upd);
	XtSetSensitive(m_install, can(A_INSTALL, sel));
	XtSetSensitive(m_upgrade, can(A_UPGRADE, sel));
	XtSetSensitive(m_remove, can(A_REMOVE, sel));
	XtSetSensitive(b_main, can(a, sel));
	XtSetSensitive(b_try, can(a, sel));
	XtSetSensitive(m_try, can(a, sel));
}

static void show_details(void)
{
	char s[500], sz[40], neededby[300];
	struct pkg *p;
	int i;
	enum act a;

	if (sel < 0) {
		set_label(d_name, npk ? "Nothing chosen" : "No packages yet");
		set_label(d_sub, " ");
		XmTextSetString(d_desc, npk ? "Choose a package on the left."
					    : "Check for Updates fetches the package lists of both systems.");
		set_label(d_status, " ");
		set_label(d_size, " ");
		set_label(d_needs, " ");
		set_label(d_neededby, " ");
		set_label(d_twin, " ");
		set_label(b_main, "Install");
		set_sensitivity();
		return;
	}
	p = &pk[sel];
	set_label(d_name, p->name);
	sprintf(s, "%s  -  %s  -  %s%s%s", src_name[p->src], p->version[0] ? p->version : "?", p->cat,
		p->cpu[0] ? "  -  " : "", !p->cpu[0] ? "" : !strcmp(p->cpu, "68060") ? "68060 only" : "68020-68060");
	set_label(d_sub, s);
	XmTextSetString(d_desc, p->desc);
	if (p->update)
		sprintf(s, "Installed %s, %s available", p->iver, p->version);
	else if (p->original)
		strcpy(s, "Installed, part of the original Amix 2.1");
	else if (p->installed)
		sprintf(s, "Installed %s", p->iver);
	else if (cpu_bad(p))
		strcpy(s, "Needs a 68060; this machine has none");
	else
		strcpy(s, "Not installed");
	set_label(d_status, s);
	if (p->src == S_APKG) {
		human(p->size, sz);
		sprintf(s, "Download %s", sz);
	} else
		strcpy(s, "Download: Try shows the size");
	set_label(d_size, s);
	if (p->deps[0]) {
		sprintf(s, "Needs %.400s", p->deps);
		for (i = 0; s[i]; i++)
			if (s[i] == ',')
				s[i] = ' ';
	} else
		strcpy(s, "Needs nothing else");
	set_label(d_needs, s);
	neededby[0] = 0;
	for (i = 0; i < npk; i++)
		if (pk[i].src == p->src && pk[i].installed && has_dep(&pk[i], p->name) && strlen(neededby) < 250) {
			strcat(neededby, neededby[0] ? " " : "");
			strcat(neededby, pk[i].name);
		}
	if (neededby[0])
		sprintf(s, "Needed by %s", neededby);
	else
		strcpy(s, " ");
	set_label(d_neededby, s);
	if (p->twin >= 0)
		sprintf(s, "Also from %s: %s %s%s", src_name[pk[p->twin].src], pk[p->twin].name,
			pk[p->twin].installed ? pk[p->twin].iver : pk[p->twin].version,
			pk[p->twin].installed ? ", installed" : "");
	else
		strcpy(s, " ");
	set_label(d_twin, s);
	a = main_action(sel);
	set_label(b_main, a == A_UPGRADE ? "Update" : a == A_REMOVE ? "Remove" : "Install");
	set_sensitivity();
}

static void update_warning(void);

static void reload(void)
{
	char s[500];

	has060 = cpu_has_060();
	load_all();
	fill_cats();
	fill_pkgs();
	refresh_status();
	if (cfg.apkg_on && apkg_state == -2) {
		sprintf(s, "%s is not an apkg catalog (version 1).", catalog);
		message(s, 2);
	}
	update_warning();
}

static void update_warning(void)
{
	char s[600];
	int warn = 0;

	s[0] = 0;
	if (cfg.ips_on && !ips_ok) {
		sprintf(s, "IPS pkg was not found at %s: its packages are not shown.", cfg.ips_path);
		warn = 1;
	}
	if (cfg.apkg_on && !apkg_ok) {
		sprintf(s + strlen(s), "%sapkg was not found at %s: its packages can be browsed, not changed.",
			warn ? "\n" : "", cfg.apkg_path);
		warn = 1;
	}
	if (!cfg.ips_on && !cfg.apkg_on) {
		strcpy(s, "Both package systems are off in the preferences.");
		warn = 1;
	}
	if (!warn) {
		/* the toolbar moves up into the bar's place */
		XtVaSetValues(toolbar, XmNtopAttachment, XmATTACH_FORM, NULL);
		XtVaSetValues(t_search, XmNtopAttachment, XmATTACH_FORM, NULL);
		XtVaSetValues(searchlabel, XmNtopAttachment, XmATTACH_FORM, NULL);
		XtUnmanageChild(warnbar);
	} else {
		XtVaSetValues(toolbar, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, NULL);
		XtVaSetValues(t_search, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, NULL);
		XtVaSetValues(searchlabel, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, NULL);
		set_label(warnlabel, s);
		XtManageChild(warnbar);
	}
	set_sensitivity();
	refresh_status();
}

/* ------------------------------------------------------------ the job queue */

/*
 * A change is a queue of jobs, run one after the other in the progress
 * window: apkg and pkg never at the same time.  Each job is one program run
 * (directly as root, else through swrun with the root password, given once
 * for the whole queue).
 */
struct job {
	char title[160];
	char *argv[MAXARG];		/* without swrun: the program and its arguments */
	char via[MAXARG][260];		/* the strings behind argv */
	int nargs;
	int swrun;			/* through swrun even as root (conf jobs) */
	int dry;
	char *input;			/* sysconf: the file, after the password line */
};
static struct job jobs[MAXJOB];
static int njobs, cur_job = -1;
static char root_pw[128];
static int stopping;

static pid_t child = -1;
static int child_fd = -1;
static XtInputId child_in;
static char linebuf[1024];
static int linelen;
static Widget pg_dlg, pg_label, pg_bar, pg_text, pg_out, pg_toggle, pg_button, pg_buttons;
static long pg_done, pg_total;
static int queue_dry, queue_failed;
static void (*queue_after)(void);

static int busy(void)
{
	return child > 0 || cur_job >= 0;
}

static struct job *new_job(const char *title)
{
	struct job *j;

	if (njobs >= MAXJOB)
		return NULL;
	j = &jobs[njobs++];
	memset(j, 0, sizeof *j);
	copy(j->title, title, sizeof j->title);
	return j;
}

static void arg(struct job *j, const char *s)
{
	if (!j || j->nargs >= MAXARG - 1)
		return;
	copy(j->via[j->nargs], s, sizeof j->via[0]);
	j->argv[j->nargs] = j->via[j->nargs];
	j->argv[++j->nargs] = NULL;
}

/* pkg [--min-free MB] VERB [-n] names...: IPS */
static void ips_job(const char *title, const char *verb, int dry, int *list, int n)
{
	struct job *j = new_job(title);
	char mf[16];
	int i;

	if (!j)
		return;
	j->dry = dry;
	arg(j, cfg.ips_path);
	if (!strcmp(verb, "install") || !strcmp(verb, "update")) {
		sprintf(mf, "%d", cfg.min_free);
		arg(j, "--min-free");
		arg(j, mf);
	}
	arg(j, verb);
	if (dry)
		arg(j, "-n");
	for (i = 0; i < n; i++)
		arg(j, pk[list[i]].name);
}

/* apkg [-n] VERB names... */
static void apkg_job(const char *title, const char *verb, int dry, int *list, int n)
{
	struct job *j = new_job(title);
	int i;

	if (!j)
		return;
	j->dry = dry;
	arg(j, cfg.apkg_path);
	if (dry)
		arg(j, "-n");
	arg(j, verb);
	for (i = 0; i < n; i++)
		arg(j, pk[list[i]].name);
}

static void pg_draw(void)
{
	Dimension w, h;
	Display *d = XtDisplay(pg_bar);
	Window win = XtWindow(pg_bar);
	Pixel fg, bg;
	GC gc;
	int fill;

	if (!win)
		return;
	XtVaGetValues(pg_bar, XmNwidth, &w, XmNheight, &h, XmNbackground, &bg, NULL);
	gc = XCreateGC(d, win, 0, NULL);
	XSetForeground(d, gc, bg);
	XFillRectangle(d, win, gc, 0, 0, w, h);
	fill = pg_total > 0 ? (int)((double)pg_done / pg_total * (w - 2)) : 0;
	XtVaGetValues(pg_bar, XmNbottomShadowColor, &fg, NULL);
	XSetForeground(d, gc, fg);
	XDrawRectangle(d, win, gc, 0, 0, w - 1, h - 1);
	XSetForeground(d, gc, bar_pixel);
	if (fill > 0)
		XFillRectangle(d, win, gc, 1, 1, fill, h - 2);
	XFreeGC(d, gc);
}

static void pg_expose(Widget w, XtPointer cd, XtPointer cb)
{
	pg_draw();
}

static void pg_append(const char *s)
{
	XmTextPosition end = XmTextGetLastPosition(pg_text);

	XmTextInsert(pg_text, end, (char *)s);
	XmTextShowPosition(pg_text, XmTextGetLastPosition(pg_text));
}

/* a line of output; apkg's byte counts move the bar within its job */
static void pg_line(char *line)
{
	long a, b;
	char s[300];

	pg_append(line);
	pg_append("\n");
	if (sscanf(line, "apkg: ... %ld/%ld bytes", &a, &b) == 2 && b > 0) {
		char x[40], y[40];

		human(a, x);
		human(b, y);
		sprintf(s, "%s: downloading %s of %s", jobs[cur_job].title, x, y);
		set_label(pg_label, s);
	} else if (strstr(line, "pkgadd") || strstr(line, "pkgrm")) {
		sprintf(s, "%s: %s", jobs[cur_job].title, strstr(line, "pkgrm") ? "removing" : "installing");
		set_label(pg_label, s);
	} else if (!strncmp(line, "install ", 8) || !strncmp(line, "update ", 7) || !strncmp(line, "remove ", 7)) {
		sprintf(s, "%s: %.200s", jobs[cur_job].title, line);
		set_label(pg_label, s);
	}
}

static void show_output(int on)
{
	/* the buttons stay at the bottom; the output, when shown, fills the
	 * space between the toggle and them */
	if (on) {
		XtVaSetValues(pg_buttons, XmNtopAttachment, XmATTACH_NONE, NULL);
		XtManageChild(pg_out);
	} else {
		XtUnmanageChild(pg_out);
		XtVaSetValues(pg_buttons, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_toggle, NULL);
	}
	XmToggleButtonSetState(pg_toggle, on, False);
}

static void run_next(void);

static void queue_end(const char *s)
{
	cur_job = -1;
	njobs = 0;
	memset(root_pw, 0, sizeof root_pw);
	set_label(pg_label, s);
	set_label(pg_button, "Close");
	pg_draw();
	reload();
	if (queue_after && !queue_failed) {
		void (*f)(void) = queue_after;

		queue_after = NULL;
		f();
	}
	queue_after = NULL;
}

static void child_done(int st)
{
	char s[400];
	int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;

	XtRemoveInput(child_in);
	close(child_fd);
	child_fd = -1;
	child = -1;
	if (linelen) {
		linebuf[linelen] = 0;
		pg_line(linebuf);
		linelen = 0;
	}
	pg_done++;
	pg_draw();
	if (code == 0 && !stopping) {
		if (cur_job + 1 < njobs) {
			cur_job++;
			run_next();
			return;
		}
		if (queue_dry)
			strcpy(s, "Dry run finished.  Nothing was changed: the output shows what would happen.");
		else
			sprintf(s, "%s: done.", njobs > 1 ? "All steps" : jobs[cur_job].title);
		pg_done = pg_total;
		queue_end(s);
		return;
	}
	queue_failed = 1;
	if (code == 3)
		strcpy(s, "Wrong root password: nothing was changed.");
	else if (stopping || WIFSIGNALED(st))
		sprintf(s, "Stopped at \"%s\".  The steps after it did not run.", jobs[cur_job].title);
	else
		sprintf(s, "\"%s\" stopped with an error (exit %d); the steps after it did not run.  "
			   "See the output.", jobs[cur_job].title, code);
	show_output(1);
	queue_end(s);
}

static void child_input(XtPointer cd, int *fd, XtInputId *id)
{
	char buf[512];
	int n = read(*fd, buf, sizeof buf), i, st;

	if (n > 0) {
		for (i = 0; i < n; i++) {
			if (buf[i] == '\n' || linelen >= (int)sizeof linebuf - 1) {
				linebuf[linelen] = 0;
				pg_line(linebuf);
				linelen = 0;
			} else if (buf[i] != '\r')
				linebuf[linelen++] = buf[i];
		}
		return;
	}
	while (waitpid(child, &st, 0) < 0)
		if (errno != EINTR) {
			st = 0x7f00;
			break;
		}
	child_done(st);
}

/* the full command line of a job: through swrun unless we are root */
static int job_argv(struct job *j, char **argv)
{
	int n = 0, i, root = getuid() == 0;
	static char target[300];
	const char *sw = env_or("AMIXSOFTWARE_SWRUN", SWRUN);

	if (j->swrun) {
		argv[n++] = (char *)sw;
		for (i = 0; i < j->nargs; i++)
			argv[n++] = j->argv[i];
	} else if (root) {
		for (i = 0; i < j->nargs; i++)
			argv[n++] = j->argv[i];
	} else {
		/* swrun takes no links: the program's real path */
		int l = readlink(j->argv[0], target, sizeof target - 1);

		argv[n++] = (char *)sw;
		if (!strcmp(j->argv[0], cfg.ips_path))
			argv[n++] = "pkg";
		if (l > 0 && target[0] == '/') {
			target[l] = 0;
			argv[n++] = target;
		} else
			argv[n++] = j->argv[0];
		for (i = 1; i < j->nargs; i++)
			argv[n++] = j->argv[i];
	}
	argv[n] = NULL;
	return n;
}

static void run_next(void)
{
	struct job *j = &jobs[cur_job];
	char *argv[MAXARG + 4], cmd[700] = "$";
	int p[2], in[2] = { -1, -1 }, i, needs_pw = getuid() != 0 || j->swrun;

	job_argv(j, argv);
	for (i = 0; argv[i]; i++) {
		strcat(cmd, " ");
		strncat(cmd, argv[i], 690 - strlen(cmd));
	}
	pg_append(cmd);
	pg_append("\n");
	sprintf(cmd, "%s (%d of %d)", j->title, cur_job + 1, njobs);
	set_label(pg_label, cmd);
	if (pipe(p) < 0 || (needs_pw && pipe(in) < 0)) {
		queue_failed = 1;
		queue_end("Cannot start: no pipe.");
		return;
	}
	if ((child = fork()) == 0) {
		int nul = open("/dev/null", O_RDONLY);

		setsid();
		if (needs_pw) {
			dup2(in[0], 0);
			close(in[1]);
		} else
			dup2(nul, 0);
		dup2(p[1], 1);
		dup2(p[1], 2);
		close(p[0]);
		execv(argv[0], argv);
		fprintf(stderr, "cannot run %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	close(p[1]);
	if (needs_pw) {
		close(in[0]);
		if (child > 0) {
			if (getuid() != 0)
				write(in[1], root_pw, strlen(root_pw));
			write(in[1], "\n", 1);
			if (j->input)
				write(in[1], j->input, strlen(j->input));
		}
		close(in[1]);
	}
	if (child < 0) {
		close(p[0]);
		queue_failed = 1;
		queue_end("Cannot start: no process.");
		return;
	}
	child_fd = p[0];
	child_in = XtAppAddInput(app, child_fd, (XtPointer)XtInputReadMask, child_input, NULL);
}

static void pg_button_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (child > 0) {
		stopping = 1;
		kill(-child, SIGTERM);
		set_label(pg_label, "Stopping...");
		return;
	}
	XtUnmanageChild(pg_dlg);
}

static void pg_toggle_cb(Widget w, XtPointer cd, XtPointer cb)
{
	show_output(XmToggleButtonGetState(w));
}

static void make_progress(void)
{
	Widget f;
	Arg a[8];
	int n = 0;

	pg_dlg = XmCreateFormDialog(top, "progress", NULL, 0);
	XtVaSetValues(pg_dlg, XmNmarginWidth, 12, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = pg_dlg;
	pg_label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNrecomputeSize, False, XmNwidth, 520,
					   XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					   XmNrightAttachment, XmATTACH_FORM, NULL);
	pg_bar = XtVaCreateManagedWidget("bar", xmDrawingAreaWidgetClass, f, XmNheight, 14, XmNwidth, 520,
					 XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_label, XmNtopOffset, 8,
					 XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	XtAddCallback(pg_bar, XmNexposeCallback, pg_expose, NULL);
	pg_toggle = XtVaCreateManagedWidget("toggle", xmToggleButtonWidgetClass, f,
					    XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_bar, XmNtopOffset, 8,
					    XmNleftAttachment, XmATTACH_FORM, NULL);
	set_label(pg_toggle, "Show the output");
	XtVaSetValues(pg_toggle, XmNselectColor, bar_pixel, NULL);
	XtAddCallback(pg_toggle, XmNvalueChangedCallback, pg_toggle_cb, NULL);
	pg_buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
					     XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_toggle, XmNtopOffset, 10,
					     XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	pg_button = XtVaCreateManagedWidget("stop", xmPushButtonWidgetClass, pg_buttons, NULL);
	XtAddCallback(pg_button, XmNactivateCallback, pg_button_cb, NULL);
	XtSetArg(a[n], XmNeditable, False); n++;
	XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	XtSetArg(a[n], XmNrows, 12); n++;
	XtSetArg(a[n], XmNcolumns, 76); n++;
	XtSetArg(a[n], XmNcursorPositionVisible, False); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	pg_text = XmCreateScrolledText(f, "output", a, n);
	XtManageChild(pg_text);
	pg_out = XtParent(pg_text);
	XtVaSetValues(pg_out, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_toggle, XmNtopOffset, 6,
		      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
		      XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, pg_buttons, XmNbottomOffset, 8, NULL);
	XtUnmanageChild(pg_out);
}

/* the queue built: run it (after the root password, when we need one) */
static void run_queue_now(const char *title)
{
	queue_failed = stopping = 0;
	pg_done = 0;
	pg_total = njobs;
	linelen = 0;
	XmTextSetString(pg_text, "");
	XtVaSetValues(XtParent(pg_dlg), XmNtitle, title, NULL);
	set_label(pg_button, "Stop");
	show_output(queue_dry);
	XtManageChild(pg_dlg);
	cur_job = 0;
	set_sensitivity();
	run_next();
}

/* ------------------------------------------------------------ password */

static Widget pw_dlg, pw_text, pw_label;
static int pw_len;
static char pw_title[200];

static void pw_modify(Widget w, XtPointer cd, XtPointer cb)
{
	XmTextVerifyCallbackStruct *v = (XmTextVerifyCallbackStruct *)cb;
	int i, del = v->endPos - v->startPos;

	if (v->startPos < pw_len && del > 0)	/* only deleting at the end makes sense */
		pw_len = v->startPos;
	for (i = 0; v->text && i < v->text->length && pw_len < (int)sizeof root_pw - 1; i++) {
		root_pw[pw_len++] = v->text->ptr[i];
		v->text->ptr[i] = '*';
	}
	root_pw[pw_len] = 0;
}

static void pw_ok(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(pw_dlg);
	pw_len = 0;
	XmTextFieldSetString(pw_text, "");
	run_queue_now(pw_title);
}

static void pw_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(pw_dlg);
	memset(root_pw, 0, sizeof root_pw);
	pw_len = 0;
	XmTextFieldSetString(pw_text, "");
	njobs = 0;
	queue_after = NULL;
	set_sensitivity();
}

static void make_password(void)
{
	Widget f, bar, ok, cancel, icon;

	pw_dlg = XmCreateFormDialog(top, "password", NULL, 0);
	XtVaSetValues(XtParent(pw_dlg), XmNtitle, "Root password", NULL);
	XtVaSetValues(pw_dlg, XmNmarginWidth, 12, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = pw_dlg;
	icon = XtVaCreateManagedWidget("icon", xmLabelWidgetClass, f, XmNlabelType, XmPIXMAP,
				       XmNlabelPixmap, XmGetPixmap(XtScreen(top), "xm_question", BlackPixelOfScreen(XtScreen(top)),
								   WhitePixelOfScreen(XtScreen(top))),
				       XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, NULL);
	pw_label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_WIDGET,
					   XmNleftWidget, icon, XmNleftOffset, 10, XmNrightAttachment, XmATTACH_FORM, NULL);
	pw_text = XtVaCreateManagedWidget("text", xmTextFieldWidgetClass, f, XmNcolumns, 28,
					  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pw_label, XmNtopOffset, 10,
					  XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, icon, XmNleftOffset, 10,
					  XmNrightAttachment, XmATTACH_FORM, NULL);
	XtAddCallback(pw_text, XmNmodifyVerifyCallback, pw_modify, NULL);
	XtAddCallback(pw_text, XmNactivateCallback, pw_ok, NULL);
	bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pw_text, XmNtopOffset, 12,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	cancel = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, bar, NULL);
	set_label(cancel, "Cancel");
	XtAddCallback(cancel, XmNactivateCallback, pw_cancel, NULL);
	ok = XtVaCreateManagedWidget("ok", xmPushButtonWidgetClass, bar, XmNshowAsDefault, True, NULL);
	set_label(ok, "Continue");
	XtAddCallback(ok, XmNactivateCallback, pw_ok, NULL);
}

/* run the queue built with ips_job/apkg_job/conf jobs: one password for all */
static void start_queue(const char *title, int dry)
{
	char s[400];
	int i, need = getuid() != 0;

	if (!njobs)
		return;
	queue_dry = dry;
	for (i = 0; i < njobs; i++)
		need |= jobs[i].swrun && getuid() != 0;
	copy(pw_title, title, sizeof pw_title);
	if (!need) {
		run_queue_now(title);
		return;
	}
	sprintf(s, "%s needs the root password.%s", title,
		dry ? "\nA dry run changes nothing, but it still runs as root." : "");
	set_label(pw_label, s);
	XtManageChild(pw_dlg);
	XmProcessTraversal(pw_text, XmTRAVERSE_CURRENT);
}

/* ------------------------------------------------------------ check for updates, update all */

static void check_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (busy())
		return;
	njobs = 0;
	if (ips_ok) {
		struct job *j = new_job("Fetching the IPS catalog");

		arg(j, cfg.ips_path);
		arg(j, "refresh");
	}
	if (apkg_ok)
		apkg_job("Fetching the apkg catalog", "update", 0, NULL, 0);
	start_queue("Check for Updates", 0);
}

static void updall(int dry)
{
	int list[MAXPKG], n = 0, i;

	njobs = 0;
	for (i = 0; i < npk; i++)
		if (pk[i].src == S_IPS && pk[i].update && !cpu_bad(&pk[i]))
			n++;
	if (n && ips_ok)
		ips_job("Updating the IPS packages", "update", dry, NULL, 0);
	for (n = i = 0; i < npk && n < 32; i++)
		if (pk[i].src == S_APKG && pk[i].update)
			list[n++] = i;
	if (n && apkg_ok)
		apkg_job("Updating the apkg packages", "upgrade", dry, list, n);
	start_queue(dry ? "Update All (dry run)" : "Update All", dry);
}

/* ------------------------------------------------------------ confirm */

static Widget cf_dlg, cf_label, cf_list, cf_total, cf_dry, cf_ok;
static int cf_pk[64], cf_n, cf_updall;
static enum act cf_act;
static int cf_first;			/* also remove the twin first (conflict choice) */

static void cf_update_button(void)
{
	int dry = XmToggleButtonGetState(cf_dry);

	set_label(cf_ok, dry ? "Try" : cf_updall ? "Update All" : cf_act == A_REMOVE ? "Remove" :
		  cf_act == A_UPGRADE ? "Update" : "Install");
}

static void cf_dry_cb(Widget w, XtPointer cd, XtPointer cb)
{
	cf_update_button();
}

static void cf_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(cf_dlg);
}

static const char *ips_verb(enum act a)
{
	return a == A_REMOVE ? "uninstall" : a == A_UPGRADE ? "update" : "install";
}

static const char *apkg_verb(enum act a)
{
	return a == A_REMOVE ? "remove" : a == A_UPGRADE ? "upgrade" : "install";
}

static void queue_change(int dry)
{
	char title[200];
	int p = cf_pk[cf_n - 1];
	const char *what = cf_act == A_REMOVE ? "Removing" : cf_act == A_UPGRADE ? "Updating" : "Installing";

	njobs = 0;
	if (cf_first && pk[p].twin >= 0) {
		int t = pk[p].twin;

		sprintf(title, "Removing %s (%s)", pk[t].name, src_name[pk[t].src]);
		if (pk[t].src == S_IPS)
			ips_job(title, "uninstall", dry, &t, 1);
		else
			apkg_job(title, "remove", dry, &t, 1);
	}
	sprintf(title, "%s %s (%s)%s", what, pk[p].name, src_name[pk[p].src], dry ? ", dry run" : "");
	/* both resolve the requirements themselves: name only the package */
	if (pk[p].src == S_IPS)
		ips_job(title, ips_verb(cf_act), dry, &p, 1);
	else
		apkg_job(title, apkg_verb(cf_act), dry, &p, 1);
	start_queue(title, dry);
}

static void original_confirmed(Widget w, XtPointer cd, XtPointer cb)
{
	XtDestroyWidget(XtParent(w));
	queue_change(0);
}

static void cf_ok_cb(Widget w, XtPointer cd, XtPointer cb)
{
	int dry = XmToggleButtonGetState(cf_dry), i;
	long need = 0, fr = free_kb("/");
	char s[500];

	XtUnmanageChild(cf_dlg);
	if (cf_updall) {
		updall(dry);
		return;
	}
	if (!dry && cf_act != A_REMOVE) {
		for (i = 0; i < cf_n; i++)
			if (pk[cf_pk[i]].size > 0)
				need += pk[cf_pk[i]].size;
		/* the download and the installed files: about twice the package */
		if (fr >= 0 && fr - 2 * need / 1024 < cfg.min_free * 1024L) {
			sprintf(s, "Not enough room on /: %ld MB free, and at least %d MB must stay free after "
				   "the install.  Make room first, or use Try to see what would happen.",
				fr / 1024, cfg.min_free);
			message(s, 2);
			return;
		}
	}
	if (cf_act == A_REMOVE && !dry && pk[cf_pk[0]].original && cfg.ask_original) {
		Widget d;
		XmString x;

		sprintf(s, "%s is part of the original Amix 2.1 system.  Removing it can leave programs, "
			   "or the whole system, unable to work.\n\nRemove %s anyway?", pk[cf_pk[0]].name,
			pk[cf_pk[0]].name);
		d = XmCreateWarningDialog(top, "original", NULL, 0);
		x = XmStringCreateLtoR(s, XmFONTLIST_DEFAULT_TAG);
		XtVaSetValues(d, XmNmessageString, x, XmNdefaultButtonType, XmDIALOG_CANCEL_BUTTON, NULL);
		XmStringFree(x);
		XtVaSetValues(XtParent(d), XmNtitle, TITLE, NULL);
		set_label(XmMessageBoxGetChild(d, XmDIALOG_OK_BUTTON), "Remove anyway");
		XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
		XtAddCallback(d, XmNokCallback, original_confirmed, NULL);
		XtAddCallback(d, XmNcancelCallback, destroy_cb, NULL);
		XtManageChild(d);
		return;
	}
	queue_change(dry);
}

static void make_confirm(void)
{
	Widget f, bar, b, fr;
	Arg a[6];
	int n = 0;

	cf_dlg = XmCreateFormDialog(top, "confirm", NULL, 0);
	XtVaSetValues(cf_dlg, XmNmarginWidth, 12, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = cf_dlg;
	cf_label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					   XmNrightAttachment, XmATTACH_FORM, NULL);
	fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, cf_label, XmNtopOffset, 8, XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM, NULL);
	XtSetArg(a[n], XmNvisibleItemCount, 7); n++;
	XtSetArg(a[n], XmNselectionPolicy, XmSINGLE_SELECT); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	cf_list = XmCreateScrolledList(fr, "list", a, n);
	XtManageChild(cf_list);
	cf_total = XtVaCreateManagedWidget("total", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, fr, XmNtopOffset, 8,
					   XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	cf_dry = XtVaCreateManagedWidget("dry", xmToggleButtonWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
					 XmNtopWidget, cf_total, XmNtopOffset, 8, XmNleftAttachment, XmATTACH_FORM, NULL);
	XtVaSetValues(cf_dry, XmNselectColor, bar_pixel, NULL);
	set_label(cf_dry, "Dry run only (pkg -n / apkg -n: shows what would happen, changes nothing)");
	XtAddCallback(cf_dry, XmNvalueChangedCallback, cf_dry_cb, NULL);
	bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, cf_dry, XmNtopOffset, 12,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	b = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, bar, NULL);
	set_label(b, "Cancel");
	XtAddCallback(b, XmNactivateCallback, cf_cancel, NULL);
	cf_ok = XtVaCreateManagedWidget("ok", xmPushButtonWidgetClass, bar, XmNshowAsDefault, True, NULL);
	XtAddCallback(cf_ok, XmNactivateCallback, cf_ok_cb, NULL);
}

static void confirm_show(int dry)
{
	XmString it[70];
	char s[400], sz[40];
	long total = 0, fr;
	int i, unknown = 0, p = cf_pk[cf_n - 1];
	enum act a = cf_act;

	for (i = 0; i < cf_n; i++) {
		struct pkg *q = &pk[cf_pk[i]];

		human(q->size, sz);
		if (a == A_REMOVE)
			sprintf(s, " %-24.24s %-4s %s", q->name, src_name[q->src], q->original ? "Amix 2.1" : q->iver);
		else
			sprintf(s, " %-24.24s %-4s %-12.12s %9s  %s", q->name, src_name[q->src], q->version, sz,
				cf_pk[i] == p ? "" : "needed");
		it[i] = XmStringCreateLocalized(s);
		if (q->size >= 0)
			total += q->size;
		else
			unknown = 1;
	}
	if (cf_first && pk[p].twin >= 0 && cf_n < 69) {
		sprintf(s, " %-24.24s %-4s removed first", pk[pk[p].twin].name, src_name[pk[pk[p].twin].src]);
		it[cf_n] = XmStringCreateLocalized(s);
	}
	XmListDeleteAllItems(cf_list);
	XmListAddItems(cf_list, it, cf_n + (cf_first && pk[p].twin >= 0), 0);
	for (i = 0; i < cf_n + (cf_first && pk[p].twin >= 0); i++)
		XmStringFree(it[i]);
	if (a == A_REMOVE)
		sprintf(s, "Remove %s (%s) from this system?", pk[p].name, src_name[pk[p].src]);
	else if (a == A_UPGRADE)
		sprintf(s, "Update %s (%s) from %s to %s?", pk[p].name, src_name[pk[p].src], pk[p].iver, pk[p].version);
	else if (cf_n > 1)
		sprintf(s, "Install %s (%s) and the %d package%s it needs?", pk[p].name, src_name[pk[p].src], cf_n - 1,
			cf_n > 2 ? "s" : "");
	else
		sprintf(s, "Install %s (%s)?", pk[p].name, src_name[pk[p].src]);
	set_label(cf_label, s);
	fr = free_kb("/");
	human(total, sz);
	if (a == A_REMOVE) {
		char by[300] = "";

		for (i = 0; i < npk; i++)
			if (pk[i].src == pk[p].src && pk[i].installed && has_dep(&pk[i], pk[p].name) && strlen(by) < 250) {
				strcat(by, by[0] ? ", " : "");
				strcat(by, pk[i].name);
			}
		if (by[0])
			sprintf(s, "Needed by %s: %s.", by, pk[p].src == S_IPS ? "pkg refuses while they need it"
									  : "they may stop working");
		else
			strcpy(s, "No installed package needs it.");
	} else
		sprintf(s, "Download: %s%s        Free on /: %ld MB (keeps %d MB)", unknown ? "Try shows it" : sz,
			unknown && total ? " and more" : "", fr / 1024, cfg.min_free);
	set_label(cf_total, s);
	XmToggleButtonSetState(cf_dry, dry, False);
	cf_update_button();
	XtVaSetValues(XtParent(cf_dlg), XmNtitle, a == A_REMOVE ? "Remove package" : a == A_UPGRADE ? "Update package" :
		      "Install packages", NULL);
	XtManageChild(cf_dlg);
}

/* ------------------------------------------------------------ in both: the conflict dialog */

static Widget cx_dlg, cx_label, cx_list, cx_how[3];
static int cx_dry;

/* paths of an installed IPS package (pkg contents), or of the SVR4 one (contents file) */
static int ips_paths(const char *stem, int remote, char *out, int max)
{
	char *argv[6];
	int n = 0;

	argv[n++] = cfg.ips_path;
	argv[n++] = "contents";
	if (remote)
		argv[n++] = "-r";
	argv[n++] = (char *)stem;
	argv[n] = NULL;
	return capture(argv, out, max, NULL, 0);
}

/* the SVR4 contents file's paths of package inst, into out ("\n" separated) */
static void svr4_paths(const char *inst, char *out, int max)
{
	FILE *f = fopen(env_or("AMIXSOFTWARE_CONTENTS", CONTENTS), "r");
	char line[1024];
	int n = 0;

	out[0] = 0;
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		char *last, *sp, *p = line;
		int l;

		trim(line);
		if (line[0] != '/')
			continue;
		last = strrchr(line, ' ');
		if (!last || strcmp(last + 1, inst))
			continue;
		sp = strchr(p, ' ');
		l = sp ? sp - p : (int)strlen(p);
		if (strchr(p, '=') && strchr(p, '=') < p + l)	/* a link: path=target */
			l = strchr(p, '=') - p;
		if (n + l + 2 >= max)
			break;
		memcpy(out + n, p, l);
		n += l;
		out[n++] = '\n';
		out[n] = 0;
	}
	fclose(f);
}

/* the lines of a that are also in b (paths, "/" first), into it[]: at most max */
static int common_paths(char *a, const char *b, XmString *it, int max)
{
	char *line, *nl, s[300];
	int n = 0;

	for (line = a; line && *line && n < max; line = nl) {
		char *q;

		nl = strchr(line, '\n');
		if (nl)
			*nl++ = 0;
		q = line[0] == '/' ? line : NULL;
		if (!q) {
			/* pkg contents prints paths without "/" after a PATH header */
			if (!strcmp(line, "PATH") || !line[0])
				continue;
			sprintf(s, "/%.290s", line);
			q = s;
		} else
			copy(s, q, sizeof s);
		{
			const char *hit = strstr(b, s);
			int l = strlen(s);

			while (hit && !((hit == b || hit[-1] == '\n') && (hit[l] == '\n' || !hit[l])))
				hit = strstr(hit + 1, s);
			if (hit && strcmp(s, "/usr") && strcmp(s, "/usr/local") && strcmp(s, "/usr/local/bin")) {
				char row[320];
				struct stat st;

				if (stat(s, &st) == 0 && S_ISDIR(st.st_mode))
					continue;	/* shared directories are fine */
				sprintf(row, " %.300s", s);
				it[n++] = XmStringCreateLocalized(row);
			}
		}
	}
	return n;
}

static void cx_ok(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(cx_dlg);
	if (XmToggleButtonGetState(cx_how[2]))
		return;
	cf_first = XmToggleButtonGetState(cx_how[1]);
	if (cfg.confirm || cx_dry)
		confirm_show(cx_dry || cfg.dryrun);
	else
		queue_change(0);
}

static void cx_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(cx_dlg);
}

static void make_conflict(void)
{
	Widget f, fr, bar, b, rc;
	Arg a[6];
	int n = 0, i;
	static const char *how[] = { "Install it too, keep the other", "Remove the other first, then install",
				     "Don't install" };

	cx_dlg = XmCreateFormDialog(top, "conflict", NULL, 0);
	XtVaSetValues(cx_dlg, XmNmarginWidth, 12, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	XtVaSetValues(XtParent(cx_dlg), XmNtitle, "Same software in both", NULL);
	f = cx_dlg;
	cx_label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					   XmNrightAttachment, XmATTACH_FORM, NULL);
	fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, cx_label, XmNtopOffset, 8, XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM, NULL);
	XtSetArg(a[n], XmNvisibleItemCount, 6); n++;
	XtSetArg(a[n], XmNselectionPolicy, XmSINGLE_SELECT); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	cx_list = XmCreateScrolledList(fr, "list", a, n);
	XtManageChild(cx_list);
	rc = XtVaCreateManagedWidget("how", xmRowColumnWidgetClass, f, XmNradioBehavior, True,
				     XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, fr, XmNtopOffset, 8,
				     XmNleftAttachment, XmATTACH_FORM, NULL);
	for (i = 0; i < 3; i++) {
		cx_how[i] = XtVaCreateManagedWidget("choice", xmToggleButtonWidgetClass, rc,
						    XmNindicatorType, XmONE_OF_MANY, NULL);
		XtVaSetValues(cx_how[i], XmNselectColor, bar_pixel, NULL);
		set_label(cx_how[i], how[i]);
	}
	bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, rc, XmNtopOffset, 12,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	b = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, bar, NULL);
	set_label(b, "Cancel");
	XtAddCallback(b, XmNactivateCallback, cx_cancel, NULL);
	b = XtVaCreateManagedWidget("ok", xmPushButtonWidgetClass, bar, XmNshowAsDefault, True, NULL);
	set_label(b, "Continue...");
	XtAddCallback(b, XmNactivateCallback, cx_ok, NULL);
}

/*
 * installing p while its twin (the other system's) is installed: what
 * overlaps, and what to do.  The same path in both stops the install in pkg
 * anyway; apkg's pkgadd would write over an IPS file without a word.
 */
static void conflict(int p, int dry)
{
	static char a[65536], b[65536];
	XmString it[60];
	char s[600];
	int t = pk[p].twin, n = 0, i;

	cx_dry = dry;
	sprintf(s, "%s is already installed from %s (%s %s).\n"
		   "Installing %s from %s gives you two copies.  Files both would write:",
		pk[t].name, src_name[pk[t].src], pk[t].name, pk[t].iver, pk[p].name, src_name[pk[p].src]);
	set_label(cx_label, s);
	if (pk[p].src == S_IPS) {
		/* the IPS package's paths (from the repository) against the SVR4 package's */
		if (ips_paths(pk[p].name, 1, a, sizeof a) == 0) {
			svr4_paths(pk[t].inst, b, sizeof b);
			n = common_paths(a, b, it, 58);
		} else
			it[n++] = XmStringCreateLocalized(" (the IPS package's file list could not be fetched)");
	} else {
		/* the installed IPS package's paths: pkgadd would write over these */
		if (ips_paths(pk[t].name, 0, a, sizeof a) == 0) {
			int k;
			char *line, *nl;

			for (k = 0, line = a; line && *line && k < 58; line = nl) {
				nl = strchr(line, '\n');
				if (nl)
					*nl++ = 0;
				if (!strcmp(line, "PATH") || strstr(line, "/share/") || !line[0])
					continue;
				if (strstr(line, "bin/") || strstr(line, "lib/")) {
					char row[360];

					sprintf(row, " /%.300s  (IPS; pkgadd may write over it)", line);
					it[k++] = XmStringCreateLocalized(row);
				}
			}
			n = k;
		}
	}
	if (!n)
		it[n++] = XmStringCreateLocalized(" none: no file in both");
	XmListDeleteAllItems(cx_list);
	XmListAddItems(cx_list, it, n, 0);
	for (i = 0; i < n; i++)
		XmStringFree(it[i]);
	XmToggleButtonSetState(cx_how[0], True, False);
	XmToggleButtonSetState(cx_how[1], False, False);
	XmToggleButtonSetState(cx_how[2], False, False);
	XtManageChild(cx_dlg);
}

static void confirm(enum act a, int dry)
{
	if (sel < 0 || busy())
		return;
	if (a == A_REMOVE && pk[sel].src == S_APKG && (!strcmp(pk[sel].name, "apkg") || !strcmp(pk[sel].name, "apkgeng"))) {
		message("The Software Center cannot remove apkg or its engine: it works through them.\n"
			"To remove them, use pkgrm as root in a terminal.", 1);
		return;
	}
	if (a == A_REMOVE && pk[sel].src == S_IPS && !strcmp(pk[sel].name, "sysutils/amix-pkg")) {
		message("The Software Center cannot remove IPS pkg: it works through it.", 1);
		return;
	}
	cf_act = a;
	cf_n = 0;
	cf_first = cf_updall = 0;
	if (a == A_INSTALL)
		resolve(sel, cf_pk, &cf_n, 0);
	else
		cf_pk[cf_n++] = sel;
	if (a == A_INSTALL && pk[sel].twin >= 0 && pk[pk[sel].twin].installed) {
		conflict(sel, dry);
		return;
	}
	if (!cfg.confirm && !dry) {
		queue_change(0);
		return;
	}
	confirm_show(dry);
}

static void act_cb(Widget w, XtPointer cd, XtPointer cb)
{
	enum act a = (enum act)(long)cd;

	if (busy())
		return;
	if (a == A_NONE)
		a = main_action(sel);
	if (!can(a, sel))
		return;
	confirm(a, cfg.dryrun);
}

static void try_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (busy() || sel < 0 || !can(main_action(sel), sel))
		return;
	confirm(main_action(sel), 1);
}

static void updall_cb(Widget w, XtPointer cd, XtPointer cb)
{
	XmString it[70];
	char s[300];
	int i, n = 0;

	if (busy())
		return;
	cf_updall = 1;
	cf_first = 0;
	cf_n = 0;
	for (i = 0; i < npk && n < 69; i++)
		if (pk[i].update && src_ok(pk[i].src) && !cpu_bad(&pk[i])) {
			sprintf(s, " %-24.24s %-4s %s -> %s", pk[i].name, src_name[pk[i].src], pk[i].iver, pk[i].version);
			it[n++] = XmStringCreateLocalized(s);
		}
	XmListDeleteAllItems(cf_list);
	XmListAddItems(cf_list, it, n, 0);
	for (i = 0; i < n; i++)
		XmStringFree(it[i]);
	sprintf(s, "Update %d package%s?  IPS first, then apkg, one after the other.", n, n == 1 ? "" : "s");
	set_label(cf_label, s);
	sprintf(s, "Free on /: %ld MB (keeps %d MB)", free_kb("/") / 1024, cfg.min_free);
	set_label(cf_total, s);
	XmToggleButtonSetState(cf_dry, cfg.dryrun, False);
	cf_update_button();
	XtVaSetValues(XtParent(cf_dlg), XmNtitle, "Update All", NULL);
	XtManageChild(cf_dlg);
}

/* ------------------------------------------------------------ repositories */

static Widget rp_dlg, rp_ips, rp_found, rp_repo, rp_cache, rp_ns, rp_note;
static char rp_origin[16][260];
static int rp_off[16], rp_n, rp_sel = -1;
static char fd_url[16][260];
static int fd_n;

/* reachable: an http origin answers for catalog.attrs; a folder has it */
static const char *reach(const char *origin)
{
	char url[600], out[64], *argv[5];
	struct stat st;

	if (!strncmp(origin, "http://", 7)) {
		sprintf(url, "%s%spublisher/amix/catalog/catalog.attrs", origin,
			origin[strlen(origin) - 1] == '/' ? "" : "/");
		argv[0] = cfg.ips_path;
		argv[1] = "http";
		argv[2] = "get";
		argv[3] = url;
		argv[4] = NULL;
		return ips_ok && capture(argv, out, sizeof out, NULL, 0) == 0 ? "reachable" : "no answer";
	}
	sprintf(url, "%s/publisher/amix/catalog/catalog.attrs", !strncmp(origin, "file://", 7) ? origin + 7 : origin);
	return stat(url, &st) == 0 ? "folder" : "not there";
}

static void rp_fill(int check)
{
	static char out[8192];
	XmString it[16];
	char *argv[4], *line, *nl, row[400], when[40] = "-";
	struct stat st;
	int i;

	rp_n = 0;
	if (ips_ok) {
		argv[0] = cfg.ips_path;
		argv[1] = "publisher";
		argv[2] = "-t";
		argv[3] = NULL;
		if (capture(argv, out, sizeof out, NULL, 0) != 0)
			out[0] = 0;
		for (line = out; line && *line && rp_n < 16; line = nl) {
			char *s = line;

			nl = strchr(line, '\n');
			if (nl)
				*nl++ = 0;
			next_field(&s);
			copy(rp_origin[rp_n], next_field(&s), sizeof rp_origin[0]);
			rp_off[rp_n] = !strcmp(next_field(&s), "off");
			if (rp_origin[rp_n][0])
				rp_n++;
		}
	}
	if (stat(IPS_ATTRS, &st) == 0)
		strftime(when, sizeof when, "%d %b %H:%M", localtime(&st.st_mtime));
	for (i = 0; i < rp_n; i++) {
		sprintf(row, " [%c] %-44.44s %-10s %s", rp_off[i] ? ' ' : 'x', rp_origin[i],
			check ? reach(rp_origin[i]) : "", i == 0 ? when : "");
		it[i] = XmStringCreateLocalized(row);
	}
	XmListDeleteAllItems(rp_ips);
	XmListAddItems(rp_ips, it, rp_n, 0);
	for (i = 0; i < rp_n; i++)
		XmStringFree(it[i]);
	rp_sel = -1;
	XmTextFieldSetString(rp_repo, repo);
	XmTextFieldSetString(rp_cache, apkg_cachedir);
	XmTextFieldSetString(rp_ns, apkg_ns);
	if (!ips_ok)
		set_label(rp_note, "IPS pkg is off or not found: see Preferences.");
	else if (!rp_n)
		set_label(rp_note, "No IPS origin yet: Add one, or Find on Network.");
	else
		set_label(rp_note, "[x] in use (refresh takes the first that answers), [ ] kept but off.");
}

static void rp_sel_cb(Widget w, XtPointer cd, XtPointer cb)
{
	rp_sel = ((XmListCallbackStruct *)cb)->item_position - 1;
}

/* one root change of the IPS origins, then the list again */
static void rp_after(void)
{
	if (XtIsManaged(rp_dlg))
		rp_fill(0);
}

static void origin_job(const char *opt, const char *url)
{
	struct job *j;
	char title[300];

	njobs = 0;
	sprintf(title, "%s %s", !strcmp(opt, "-g") || !strcmp(opt, "-O") ? "Adding" :
		!strcmp(opt, "-G") ? "Removing" : !strcmp(opt, "--enable-origin") ? "Using" : "Not using", url);
	j = new_job(title);
	arg(j, cfg.ips_path);
	arg(j, "set-publisher");
	arg(j, opt);
	arg(j, url);
	queue_after = rp_after;
	start_queue("Repositories", 0);
}

static void add_url_ok(Widget w, XtPointer cd, XtPointer cb)
{
	XmSelectionBoxCallbackStruct *c = (XmSelectionBoxCallbackStruct *)cb;
	char *u;

	if (XmStringGetLtoR(c->value, XmFONTLIST_DEFAULT_TAG, &u)) {
		trim(u);
		if (strncmp(u, "http://", 7) && strncmp(u, "file://", 7) && u[0] != '/')
			message("An origin is http://host:port/path, file:///folder or a folder.\n"
				"(No https: AMIX has no TLS.)", 2);
		else
			origin_job("-g", u);
		XtFree(u);
	}
	XtDestroyWidget(XtParent(w));
}

static void rp_add(Widget w, XtPointer cd, XtPointer cb)
{
	Widget d = XmCreatePromptDialog(rp_dlg, "add", NULL, 0);
	XmString x = XmStringCreateLtoR("The address of an IPS repository of AMIX packages\n"
					"(the AMIX Installer shows it on its Serve Packages page):",
					XmFONTLIST_DEFAULT_TAG);
	XmString v = XmStringCreateLocalized("http://");

	XtVaSetValues(d, XmNselectionLabelString, x, XmNtextString, v, NULL);
	XmStringFree(x);
	XmStringFree(v);
	XtVaSetValues(XtParent(d), XmNtitle, "Add an origin", NULL);
	XtUnmanageChild(XmSelectionBoxGetChild(d, XmDIALOG_HELP_BUTTON));
	XtAddCallback(d, XmNokCallback, add_url_ok, NULL);
	XtAddCallback(d, XmNcancelCallback, destroy_cb, NULL);
	XtManageChild(d);
}

static void rp_remove(Widget w, XtPointer cd, XtPointer cb)
{
	if (rp_sel < 0 || rp_sel >= rp_n || busy())
		return;
	origin_job("-G", rp_origin[rp_sel]);
}

static void rp_onoff(Widget w, XtPointer cd, XtPointer cb)
{
	if (rp_sel < 0 || rp_sel >= rp_n || busy())
		return;
	origin_job(rp_off[rp_sel] ? "--enable-origin" : "--disable-origin", rp_origin[rp_sel]);
}

static void rp_check(Widget w, XtPointer cd, XtPointer cb)
{
	rp_fill(1);
}

/* Find on Network: every AMIX Installer serving packages answers */
static void rp_find(Widget w, XtPointer cd, XtPointer cb)
{
	XmString it[16];
	int s = socket(AF_INET, SOCK_DGRAM, 0), on = 1, i;
	struct sockaddr_in to, from;
	static const char ask[] = "AMIX-REPO?\n";
	time_t until;

	fd_n = 0;
	if (s < 0) {
		message("Find on Network: no UDP socket.", 2);
		return;
	}
	setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof on);
	memset(&to, 0, sizeof to);
	to.sin_family = AF_INET;
	to.sin_port = htons(DISCOVERY_PORT);
	to.sin_addr.s_addr = htonl(0xffffffffUL);		/* the LAN */
	sendto(s, ask, sizeof ask - 1, 0, (struct sockaddr *)&to, sizeof to);
	to.sin_addr.s_addr = inet_addr("10.0.2.2");		/* FS-UAE's host */
	sendto(s, ask, sizeof ask - 1, 0, (struct sockaddr *)&to, sizeof to);
	until = time(NULL) + 3;
	while (time(NULL) < until && fd_n < 16) {
		fd_set r;
		struct timeval tv;
		char buf[512], row[400], name[64] = "?", path[128] = "/repo", date[32] = "?";
		int len = sizeof from, n, port = 8069, pkgs = 0;
		char *p;

		FD_ZERO(&r);
		FD_SET(s, &r);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if (select(s + 1, &r, NULL, NULL, &tv) <= 0)
			continue;
		if ((n = recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &len)) <= 0)
			continue;
		buf[n] = 0;
		if (strncmp(buf, "AMIX-REPO 1", 11))
			continue;
		/* AMIX-REPO 1 port=8069 path=/repo packages=95 date=2026.11.20 name=host */
		if ((p = strstr(buf, "port=")))
			port = atoi(p + 5);
		if ((p = strstr(buf, "packages=")))
			pkgs = atoi(p + 9);
		if ((p = strstr(buf, "path=")))
			sscanf(p + 5, "%127s", path);
		if ((p = strstr(buf, "date=")))
			sscanf(p + 5, "%31s", date);
		if ((p = strstr(buf, "name=")))
			sscanf(p + 5, "%63s", name);
		sprintf(fd_url[fd_n], "http://%s:%d%s", inet_ntoa(from.sin_addr), port, path);
		for (i = 0; i < fd_n && strcmp(fd_url[i], fd_url[fd_n]); i++)
			;
		if (i < fd_n)
			continue;
		sprintf(row, " %-36.36s %d packages, %s, on %s", fd_url[fd_n], pkgs, date, name);
		it[fd_n++] = XmStringCreateLocalized(row);
	}
	close(s);
	if (!fd_n)
		it[0] = XmStringCreateLocalized(" No AMIX Installer answered (is Serve Packages on?)");
	XmListDeleteAllItems(rp_found);
	XmListAddItems(rp_found, it, fd_n ? fd_n : 1, 0);
	for (i = 0; i < (fd_n ? fd_n : 1); i++)
		XmStringFree(it[i]);
}

static void rp_use(Widget w, XtPointer cd, XtPointer cb)
{
	int *pos, n;

	if (busy() || !fd_n || !XmListGetSelectedPos(rp_found, &pos, &n))
		return;
	if (pos[0] >= 1 && pos[0] <= fd_n)
		origin_job("-g", fd_url[pos[0] - 1]);
	XtFree((char *)pos);
}

static void rp_refresh(Widget w, XtPointer cd, XtPointer cb)
{
	if (!busy())
		check_cb(w, cd, cb);
}

/* apkg's settings: each changed key one swrun apkgconf job */
static void rp_apkg_save(Widget w, XtPointer cd, XtPointer cb)
{
	static const char *keys[] = { "repo", "cachedir", "nameserver" };
	char *v[3];
	const char *old[3];
	int i;

	if (busy())
		return;
	old[0] = repo;
	old[1] = apkg_cachedir;
	old[2] = apkg_ns;
	v[0] = XmTextFieldGetString(rp_repo);
	v[1] = XmTextFieldGetString(rp_cache);
	v[2] = XmTextFieldGetString(rp_ns);
	njobs = 0;
	for (i = 0; i < 3; i++) {
		trim(v[i]);
		if (v[i][0] && strcmp(v[i], old[i])) {
			char title[300];
			struct job *j;

			sprintf(title, "Setting %s= in " APKG_CONF, keys[i]);
			j = new_job(title);
			j->swrun = 1;
			arg(j, "apkgconf");
			arg(j, keys[i]);
			arg(j, v[i]);
		}
		XtFree(v[i]);
	}
	if (!njobs) {
		message("Nothing changed in apkg's settings.", 0);
		return;
	}
	queue_after = rp_after;
	start_queue("apkg settings", 0);
}

static void rp_close(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(rp_dlg);
}

static Widget heading(Widget f, Widget above, const char *text)
{
	Widget l = XtVaCreateManagedWidget("heading", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM, XmNtopWidget, above,
					   XmNtopOffset, above ? 14 : 0, XmNleftAttachment, XmATTACH_FORM, NULL);
	set_label(l, text);
	return l;
}

static Widget buttonrow(Widget f, Widget above)
{
	return XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				       XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, above, XmNtopOffset, 4,
				       XmNleftAttachment, XmATTACH_FORM, NULL);
}

static Widget pb(Widget parent, const char *label, XtCallbackProc cb)
{
	Widget b = XtVaCreateManagedWidget("button", xmPushButtonWidgetClass, parent, NULL);

	set_label(b, label);
	XtAddCallback(b, XmNactivateCallback, cb, NULL);
	return b;
}

static Widget keyfield(Widget f, Widget above, const char *key, Widget *text)
{
	Widget row = buttonrow(f, above), l;

	l = XtVaCreateManagedWidget("key", xmLabelWidgetClass, row, XmNwidth, 110, XmNrecomputeSize, False,
				    XmNalignment, XmALIGNMENT_BEGINNING, NULL);
	set_label(l, key);
	*text = XtVaCreateManagedWidget("value", xmTextFieldWidgetClass, row, XmNcolumns, 44, NULL);
	return row;
}

static void make_repos(void)
{
	Widget f, h, fr, row, b, sep;
	Arg a[6];
	int n = 0;

	rp_dlg = XmCreateFormDialog(top, "repositories", NULL, 0);
	XtVaSetValues(XtParent(rp_dlg), XmNtitle, "Repositories", NULL);
	XtVaSetValues(rp_dlg, XmNmarginWidth, 14, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = rp_dlg;
	h = heading(f, NULL, "IPS pkg: the origins of publisher amix");
	fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, h, XmNtopOffset, 4, XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM, NULL);
	XtSetArg(a[n], XmNvisibleItemCount, 4); n++;
	XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	XtSetArg(a[n], XmNwidth, 560); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	rp_ips = XmCreateScrolledList(fr, "origins", a, n);
	XtManageChild(rp_ips);
	XtAddCallback(rp_ips, XmNbrowseSelectionCallback, rp_sel_cb, NULL);
	rp_note = XtVaCreateManagedWidget("note", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					  XmNrecomputeSize, False, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, fr,
					  XmNtopOffset, 2, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
					  NULL);
	row = buttonrow(f, rp_note);
	pb(row, "Add...", rp_add);
	pb(row, "Remove", rp_remove);
	pb(row, "Use / Don't use", rp_onoff);
	pb(row, "Check", rp_check);
	pb(row, "Refresh", rp_refresh);
	h = heading(f, row, "Found on the network (AMIX Installers serving packages)");
	fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, h, XmNtopOffset, 4, XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM, NULL);
	n = 0;
	XtSetArg(a[n], XmNvisibleItemCount, 3); n++;
	XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	rp_found = XmCreateScrolledList(fr, "found", a, n);
	XtManageChild(rp_found);
	row = buttonrow(f, fr);
	pb(row, "Find on Network", rp_find);
	pb(row, "Use", rp_use);
	h = heading(f, row, "apkg (amigaux): " APKG_CONF);
	row = keyfield(f, h, "repo=", &rp_repo);
	row = keyfield(f, row, "cachedir=", &rp_cache);
	row = keyfield(f, row, "nameserver=", &rp_ns);
	b = buttonrow(f, row);
	pb(b, "Save apkg Settings", rp_apkg_save);
	sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
				      XmNtopWidget, b, XmNtopOffset, 10, XmNleftAttachment, XmATTACH_FORM,
				      XmNrightAttachment, XmATTACH_FORM, NULL);
	row = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, sep, XmNtopOffset, 8,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	b = pb(row, "Close", rp_close);
	XtVaSetValues(b, XmNshowAsDefault, True, NULL);
}

static void repos_cb(Widget w, XtPointer cd, XtPointer cb)
{
	rp_fill(0);
	XtManageChild(rp_dlg);
}

/* ------------------------------------------------------------ preferences */

static Widget pf_dlg, pf_ips_on, pf_apkg_on, pf_ips_path, pf_apkg_path, pf_ips_found, pf_apkg_found;
static Widget pf_prefer[2], pf_both, pf_minfree, pf_confirm, pf_dry, pf_060, pf_orig, pf_upd, pf_scope[2];
static Widget fsb;
static Widget fsb_for;

static void pf_check(void)
{
	char *p, v[64], s[300];

	p = XmTextFieldGetString(pf_ips_path);
	if (check_program(p, v, NULL))
		sprintf(s, "Found: pkg %s", v);
	else
		sprintf(s, "Not found: no IPS pkg at %s", p);
	set_label(pf_ips_found, s);
	XtFree(p);
	p = XmTextFieldGetString(pf_apkg_path);
	if (check_program(p, v, "apkg "))
		sprintf(s, "Found: apkg %s", v);
	else
		sprintf(s, "Not found: no apkg at %s", p);
	set_label(pf_apkg_found, s);
	XtFree(p);
}

static void pf_changed(Widget w, XtPointer cd, XtPointer cb)
{
	pf_check();
}

static void pf_show(const struct settings *s)
{
	char mf[16];

	XmToggleButtonSetState(pf_ips_on, s->ips_on, False);
	XmToggleButtonSetState(pf_apkg_on, s->apkg_on, False);
	XmTextFieldSetString(pf_ips_path, (char *)s->ips_path);
	XmTextFieldSetString(pf_apkg_path, (char *)s->apkg_path);
	XmToggleButtonSetState(pf_prefer[0], s->prefer == S_IPS, False);
	XmToggleButtonSetState(pf_prefer[1], s->prefer == S_APKG, False);
	XmToggleButtonSetState(pf_both, s->show_both, False);
	sprintf(mf, "%d", s->min_free);
	XmTextFieldSetString(pf_minfree, mf);
	XmToggleButtonSetState(pf_confirm, s->confirm, False);
	XmToggleButtonSetState(pf_dry, s->dryrun, False);
	XmToggleButtonSetState(pf_060, s->show060, False);
	XmToggleButtonSetState(pf_orig, s->ask_original, False);
	XmToggleButtonSetState(pf_upd, s->update_at_start, False);
	pf_check();
}

static void pf_read(struct settings *s)
{
	char *p;

	s->ips_on = XmToggleButtonGetState(pf_ips_on);
	s->apkg_on = XmToggleButtonGetState(pf_apkg_on);
	p = XmTextFieldGetString(pf_ips_path);
	trim(p);
	copy(s->ips_path, p[0] ? p : DEF_PKG, sizeof s->ips_path);
	XtFree(p);
	p = XmTextFieldGetString(pf_apkg_path);
	trim(p);
	copy(s->apkg_path, p[0] ? p : DEF_APKG, sizeof s->apkg_path);
	XtFree(p);
	s->prefer = XmToggleButtonGetState(pf_prefer[1]) ? S_APKG : S_IPS;
	s->show_both = XmToggleButtonGetState(pf_both);
	p = XmTextFieldGetString(pf_minfree);
	s->min_free = atoi(p) > 0 ? atoi(p) : 100;
	XtFree(p);
	s->confirm = XmToggleButtonGetState(pf_confirm);
	s->dryrun = XmToggleButtonGetState(pf_dry);
	s->show060 = XmToggleButtonGetState(pf_060);
	s->ask_original = XmToggleButtonGetState(pf_orig);
	s->update_at_start = XmToggleButtonGetState(pf_upd);
}

static void pf_applied(void)
{
	/* saved for everyone: the user's own file goes, the system's apply now */
	if (user_file)
		unlink(user_settings());
	user_file = 0;
	load_settings();
	reload();
}

/* save: for everyone through swrun (and the user's own file goes), or the user's */
static void pf_save(int close_it)
{
	static char text[2048];
	struct settings s;

	pf_read(&s);
	if (XmToggleButtonGetState(pf_scope[0])) {
		struct job *j;

		if (busy())
			return;
		settings_text(&s, text);
		njobs = 0;
		j = new_job("Saving the settings for everyone (" SYS_CONF ")");
		j->swrun = 1;
		j->input = text;
		arg(j, "sysconf");
		queue_after = pf_applied;
		start_queue("Preferences", 0);
	} else {
		cfg = s;
		if (save_user_settings() < 0)
			message("The settings could not be saved in your home directory.", 2);
		reload();
	}
	if (close_it)
		XtUnmanageChild(pf_dlg);
}

static void pf_ok(Widget w, XtPointer cd, XtPointer cb)
{
	pf_save(1);
}

static void pf_apply(Widget w, XtPointer cd, XtPointer cb)
{
	pf_save(0);
}

static void pf_reset(Widget w, XtPointer cd, XtPointer cb)
{
	pf_show(&syscfg);
}

static void pf_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(pf_dlg);
}

static void fsb_ok(Widget w, XtPointer cd, XtPointer cb)
{
	XmFileSelectionBoxCallbackStruct *c = (XmFileSelectionBoxCallbackStruct *)cb;
	char *f;

	if (XmStringGetLtoR(c->value, XmFONTLIST_DEFAULT_TAG, &f)) {
		XmTextFieldSetString(fsb_for, f);
		XtFree(f);
	}
	XtUnmanageChild(w);
	pf_check();
}

static void fsb_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(w);
}

static void pf_browse(Widget w, XtPointer cd, XtPointer cb)
{
	XmString d;

	fsb_for = (Widget)cd;
	if (!fsb) {
		fsb = XmCreateFileSelectionDialog(pf_dlg, "choose", NULL, 0);
		XtVaSetValues(XtParent(fsb), XmNtitle, "Choose the program", NULL);
		XtAddCallback(fsb, XmNokCallback, fsb_ok, NULL);
		XtAddCallback(fsb, XmNcancelCallback, fsb_cancel, NULL);
		XtUnmanageChild(XmFileSelectionBoxGetChild(fsb, XmDIALOG_HELP_BUTTON));
	}
	d = XmStringCreateLocalized(fsb_for == pf_ips_path ? "/usr/bin" : "/usr/apkg/bin");
	XtVaSetValues(fsb, XmNdirectory, d, NULL);
	XmStringFree(d);
	XtManageChild(fsb);
}

static Widget toggle(Widget parent, const char *label)
{
	Widget t = XtVaCreateManagedWidget("toggle", xmToggleButtonWidgetClass, parent, NULL);

	XtVaSetValues(t, XmNselectColor, bar_pixel, NULL);
	set_label(t, label);
	return t;
}

static Widget column(Widget f, Widget above, int radio)
{
	return XtVaCreateManagedWidget("col", xmRowColumnWidgetClass, f, XmNradioBehavior, radio,
				       XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, above, XmNtopOffset, 2,
				       XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
}

static Widget program_row(Widget f, Widget above, const char *what, Widget *on, Widget *path, Widget *found)
{
	Widget row = buttonrow(f, above), b;

	*on = toggle(row, what);
	XtVaSetValues(*on, XmNwidth, 130, XmNrecomputeSize, False, NULL);
	*path = XtVaCreateManagedWidget("path", xmTextFieldWidgetClass, row, XmNcolumns, 30, NULL);
	XtAddCallback(*path, XmNactivateCallback, pf_changed, NULL);
	XtAddCallback(*path, XmNlosingFocusCallback, pf_changed, NULL);
	b = XtVaCreateManagedWidget("browse", xmPushButtonWidgetClass, row, NULL);
	set_label(b, "Choose...");
	XtAddCallback(b, XmNactivateCallback, pf_browse, (XtPointer)*path);
	*found = XtVaCreateManagedWidget("found", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					 XmNrecomputeSize, False, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, row,
					 XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 140,
					 XmNrightAttachment, XmATTACH_FORM, NULL);
	return *found;
}

static void make_prefs(void)
{
	Widget f, h, c, row, l, sep, bar, b;

	pf_dlg = XmCreateFormDialog(top, "preferences", NULL, 0);
	XtVaSetValues(XtParent(pf_dlg), XmNtitle, "Software Center Preferences", NULL);
	XtVaSetValues(pf_dlg, XmNmarginWidth, 14, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = pf_dlg;
	h = heading(f, NULL, "Package systems (off: not shown, never run)");
	h = program_row(f, h, "IPS pkg", &pf_ips_on, &pf_ips_path, &pf_ips_found);
	h = program_row(f, h, "apkg (amigaux)", &pf_apkg_on, &pf_apkg_path, &pf_apkg_found);
	h = heading(f, h, "When both offer the same software");
	c = column(f, h, True);
	pf_prefer[0] = toggle(c, "Install from IPS pkg");
	pf_prefer[1] = toggle(c, "Install from apkg");
	XtVaSetValues(pf_prefer[0], XmNindicatorType, XmONE_OF_MANY, NULL);
	XtVaSetValues(pf_prefer[1], XmNindicatorType, XmONE_OF_MANY, NULL);
	pf_both = toggle(c, "Show both rows (else only the preferred one, unless the other is installed)");
	h = heading(f, c, "Safety");
	row = buttonrow(f, h);
	XtVaSetValues(row, XmNleftOffset, 8, NULL);
	l = XtVaCreateManagedWidget("l", xmLabelWidgetClass, row, NULL);
	set_label(l, "Keep free on each file system:");
	pf_minfree = XtVaCreateManagedWidget("minfree", xmTextFieldWidgetClass, row, XmNcolumns, 5, NULL);
	l = XtVaCreateManagedWidget("l", xmLabelWidgetClass, row, NULL);
	set_label(l, "MB");
	c = column(f, row, False);
	pf_confirm = toggle(c, "Show what will change and ask before every install, update or removal");
	pf_dry = toggle(c, "Dry run only: try every change, change nothing");
	pf_060 = toggle(c, "Show packages that need a 68060 on this machine (greyed out)");
	pf_orig = toggle(c, "Ask twice before removing a package of the original Amix 2.1");
	pf_upd = toggle(c, "Check for updates when the Software Center starts");
	h = heading(f, c, "Save for");
	c = column(f, h, True);
	pf_scope[0] = toggle(c, "Everyone on this Amiga (" SYS_CONF ", the root password)");
	pf_scope[1] = toggle(c, "Only me (~/.amixsoftware)");
	XtVaSetValues(pf_scope[0], XmNindicatorType, XmONE_OF_MANY, NULL);
	XtVaSetValues(pf_scope[1], XmNindicatorType, XmONE_OF_MANY, NULL);
	sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
				      XmNtopWidget, c, XmNtopOffset, 10, XmNleftAttachment, XmATTACH_FORM,
				      XmNrightAttachment, XmATTACH_FORM, NULL);
	bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, sep, XmNtopOffset, 8,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	b = pb(bar, "OK", pf_ok);
	XtVaSetValues(b, XmNshowAsDefault, True, NULL);
	pb(bar, "Apply", pf_apply);
	pb(bar, "Reset", pf_reset);
	pb(bar, "Cancel", pf_cancel);
}

static void prefs_cb(Widget w, XtPointer cd, XtPointer cb)
{
	pf_show(&cfg);
	XmToggleButtonSetState(pf_scope[0], getuid() == 0 && !user_file, False);
	XmToggleButtonSetState(pf_scope[1], !(getuid() == 0 && !user_file), False);
	XtManageChild(pf_dlg);
}

/* ------------------------------------------------------------ window */

static void cats_cb(Widget w, XtPointer cd, XtPointer cb)
{
	XmListCallbackStruct *c = (XmListCallbackStruct *)cb;

	cur_cat = c->item_position - 1;
	fill_pkgs();
}

static void pkgs_cb(Widget w, XtPointer cd, XtPointer cb)
{
	XmListCallbackStruct *c = (XmListCallbackStruct *)cb;

	if (c->item_position >= 1 && c->item_position <= nview) {
		sel = view[c->item_position - 1];
		show_details();
	}
}

static void pkgs_default_cb(Widget w, XtPointer cd, XtPointer cb)
{
	pkgs_cb(w, cd, cb);
	act_cb(w, (XtPointer)A_NONE, NULL);
}

static void search_cb(Widget w, XtPointer cd, XtPointer cb)
{
	fill_pkgs();
}

static void source_cb(Widget w, XtPointer cd, XtPointer cb)
{
	src_filter = (int)(long)cd;
	fill_cats();
	fill_pkgs();
}

static void quit_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (busy()) {
		message("A change is still running.  Wait for it, or stop it in its window, before closing.", 1);
		return;
	}
	exit(0);
}

static void refresh_cb(Widget w, XtPointer cd, XtPointer cb)
{
	reload();
}

static void about_cb(Widget w, XtPointer cd, XtPointer cb)
{
	message(TITLE "\n\nFinds, installs, updates and removes AMIX software from both package systems:\n"
		"  IPS pkg, our packages (amix-userland), from the origins of publisher amix;\n"
		"  apkg, the amigaux project's client (pkg.amigaux.org).\n\n"
		"Browsing needs nothing special.  Changes need the root password.\n"
		"Try runs the system's dry run (pkg -n, apkg -n): it shows what would happen\n"
		"and changes nothing.  Changes run one after the other, never both systems at once.",
		0);
}

static Widget menu(Widget bar, const char *name, char mnemonic, int help)
{
	Widget pd = XmCreatePulldownMenu(bar, (char *)name, NULL, 0);
	Widget c = XtVaCreateManagedWidget(name, xmCascadeButtonWidgetClass, bar, XmNsubMenuId, pd,
					   XmNmnemonic, mnemonic, NULL);

	if (help)
		XtVaSetValues(bar, XmNmenuHelpWidget, c, NULL);
	return pd;
}

static Widget item(Widget pd, const char *label, XtCallbackProc cb, XtPointer cd)
{
	Widget b = XtVaCreateManagedWidget("item", xmPushButtonWidgetClass, pd, NULL);

	set_label(b, label);
	XtAddCallback(b, XmNactivateCallback, cb, cd);
	return b;
}

static Widget tool(Widget bar, const char *label, XtCallbackProc cb, XtPointer cd)
{
	Widget b = XtVaCreateManagedWidget("tool", xmPushButtonWidgetClass, bar, XmNmarginWidth, 8, NULL);

	set_label(b, label);
	XtAddCallback(b, XmNactivateCallback, cb, cd);
	return b;
}

static Widget field(Widget f, Widget above, int gap)
{
	return XtVaCreateManagedWidget("field", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
				       XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, above, XmNtopOffset, gap,
				       XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
				       XmNrecomputeSize, False, NULL);
}

static XFontStruct *load_font(Display *d, const char **names)
{
	XFontStruct *f;

	for (; *names; names++)
		if ((f = XLoadQueryFont(d, *names)))
			return f;
	return NULL;
}

static void start_check_cb(XtPointer cd, XtIntervalId *id)
{
	check_cb(NULL, NULL, NULL);
}

static Widget source_menu(Widget parent)
{
	Widget pd = XmCreatePulldownMenu(parent, "sourcepd", NULL, 0), om, b;
	XmString x = XmStringCreateLocalized("Source:");
	Arg a[3];
	int n = 0;
	static const char *names[] = { "Both", "IPS pkg", "apkg" };
	int i;

	for (i = 0; i < 3; i++) {
		b = XtVaCreateManagedWidget("src", xmPushButtonWidgetClass, pd, NULL);
		set_label(b, names[i]);
		XtAddCallback(b, XmNactivateCallback, source_cb, (XtPointer)(long)(i - 1));
	}
	XtSetArg(a[n], XmNsubMenuId, pd); n++;
	XtSetArg(a[n], XmNlabelString, x); n++;
	om = XmCreateOptionMenu(parent, "source", a, n);
	XmStringFree(x);
	XtManageChild(om);
	return om;
}

int main(int argc, char **argv)
{
	/* bitmap fonts by their full names: a wildcard pattern can get a
	 * scaled instance (avg width 0, other resolution), which X11R6.3
	 * renders far slower.  The UI fonts are CDE's own. */
	static const char *monos[] = {
		"-adobe-courier-medium-r-normal--12-120-75-75-m-70-iso8859-1",
		"-misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1",
		"fixed", NULL
	};
	static const char *bigs[] = {
		"-adobe-helvetica-bold-r-normal--18-180-75-75-p-103-iso8859-1",
		"-adobe-helvetica-bold-r-normal--14-140-75-75-p-82-iso8859-1", NULL
	};
	Widget mw, mbar, pd, form, work, catfr, pkgfr, detfr, det, t, hdr, s1;
	XFontStruct *big;
	Arg a[8];
	int n;

	signal(SIGPIPE, SIG_IGN);
	load_settings();
	if (argc == 2 && !strcmp(argv[1], "-list"))
		return list_main();
	top = XtVaOpenApplication(&app, "Amixsoftware", NULL, 0, &argc, argv, NULL,
				  sessionShellWidgetClass, XmNtitle, TITLE, XmNiconName, "Software", NULL);
	if (argc > 1) {
		fprintf(stderr, "usage: amixsoftware [-list]\n");
		return 2;
	}
	mono = load_font(XtDisplay(top), monos);
	big = load_font(XtDisplay(top), bigs);
	{
		XColor c, exact;
		Colormap cm = DefaultColormapOfScreen(XtScreen(top));

		if (XAllocNamedColor(XtDisplay(top), cm, "#3a6ea5", &c, &exact))
			bar_pixel = c.pixel;
		else
			bar_pixel = BlackPixelOfScreen(XtScreen(top));
	}

	mw = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, top, NULL);
	mbar = XmCreateMenuBar(mw, "menubar", NULL, 0);
	pd = menu(mbar, "File", 'F', 0);
	m_check = item(pd, "Check for Updates", check_cb, NULL);
	item(pd, "Reload", refresh_cb, NULL);
	XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, pd, NULL);
	item(pd, "Close", quit_cb, NULL);
	pd = menu(mbar, "Packages", 'P', 0);
	m_install = item(pd, "Install...", act_cb, (XtPointer)A_INSTALL);
	m_upgrade = item(pd, "Update...", act_cb, (XtPointer)A_UPGRADE);
	m_remove = item(pd, "Remove...", act_cb, (XtPointer)A_REMOVE);
	XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, pd, NULL);
	m_try = item(pd, "Try (dry run)...", try_cb, NULL);
	m_updall = item(pd, "Update All...", updall_cb, NULL);
	pd = menu(mbar, "Repositories", 'R', 0);
	item(pd, "Repositories...", repos_cb, NULL);
	pd = menu(mbar, "Options", 'O', 0);
	item(pd, "Preferences...", prefs_cb, NULL);
	pd = menu(mbar, "Help", 'H', 1);
	item(pd, "About the Software Center", about_cb, NULL);
	XtManageChild(mbar);

	form = XtVaCreateManagedWidget("form", xmFormWidgetClass, mw, XmNmarginWidth, 8, XmNmarginHeight, 6, NULL);

	/* the warning bar: shown only while a package system is missing */
	warnbar = XtVaCreateWidget("warnbar", xmFormWidgetClass, form, XmNtopAttachment, XmATTACH_FORM,
				   XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
				   XmNshadowType, XmSHADOW_OUT, XmNshadowThickness, 1,
				   XmNmarginWidth, 8, XmNmarginHeight, 6,
				   XtVaTypedArg, XmNbackground, XmRString, "#f2d675", 8, NULL);
	t = XtVaCreateManagedWidget("setpath", xmPushButtonWidgetClass, warnbar, XmNrightAttachment, XmATTACH_FORM,
				    XmNtopAttachment, XmATTACH_FORM, NULL);
	set_label(t, "Preferences...");
	XtAddCallback(t, XmNactivateCallback, prefs_cb, NULL);
	warnlabel = XtVaCreateManagedWidget("warning", xmLabelWidgetClass, warnbar, XmNalignment, XmALIGNMENT_BEGINNING,
					    XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
					    XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_WIDGET,
					    XmNrightWidget, t, XmNrightOffset, 10,
					    XtVaTypedArg, XmNbackground, XmRString, "#f2d675", 8,
					    XtVaTypedArg, XmNforeground, XmRString, "#4a3200", 8, NULL);

	toolbar = XtVaCreateManagedWidget("toolbar", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
					  XmNspacing, 6, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar,
					  XmNtopOffset, 6, XmNleftAttachment, XmATTACH_FORM, NULL);
	tb_check = tool(toolbar, "Check for Updates", check_cb, NULL);
	tb_updall = tool(toolbar, "Update All...", updall_cb, NULL);
	tb_repos = tool(toolbar, "Repositories...", repos_cb, NULL);
	srcmenu = source_menu(toolbar);
	t_search = XtVaCreateManagedWidget("search", xmTextFieldWidgetClass, form, XmNcolumns, 20,
					   XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, XmNtopOffset, 8,
					   XmNrightAttachment, XmATTACH_FORM, NULL);
	XtAddCallback(t_search, XmNvalueChangedCallback, search_cb, NULL);
	s1 = XtVaCreateManagedWidget("searchlabel", xmLabelWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, warnbar, XmNtopOffset, 12, XmNrightAttachment, XmATTACH_WIDGET,
				     XmNrightWidget, t_search, XmNrightOffset, 4, NULL);
	set_label(s1, "Search:");
	searchlabel = s1;

	l_status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, form, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNrecomputeSize, False,
					   XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					   XmNrightAttachment, XmATTACH_FORM, NULL);

	/* categories | packages | details */
	work = XtVaCreateManagedWidget("work", xmFormWidgetClass, form, XmNresizePolicy, XmRESIZE_NONE,
				       XmNtopAttachment, XmATTACH_WIDGET,
				       XmNtopWidget, toolbar, XmNtopOffset, 6, XmNleftAttachment, XmATTACH_FORM,
				       XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_WIDGET,
				       XmNbottomWidget, l_status, XmNbottomOffset, 6, NULL);
	catfr = XtVaCreateManagedWidget("catframe", xmFrameWidgetClass, work, XmNtopAttachment, XmATTACH_FORM,
					XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, NULL);
	t = XtVaCreateManagedWidget("title", xmLabelWidgetClass, catfr, XmNchildType, XmFRAME_TITLE_CHILD, NULL);
	set_label(t, "Show");
	n = 0;
	XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
	XtSetArg(a[n], XmNvisibleItemCount, 18); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	XtSetArg(a[n], XmNwidth, 150); n++;
	XtSetArg(a[n], XmNlistMarginWidth, 4); n++;
	XtSetArg(a[n], XmNlistSizePolicy, XmCONSTANT); n++;
	l_cats = XmCreateScrolledList(catfr, "cats", a, n);
	XtManageChild(l_cats);
	XtAddCallback(l_cats, XmNbrowseSelectionCallback, cats_cb, NULL);

	detfr = XtVaCreateManagedWidget("detframe", xmFrameWidgetClass, work, XmNtopAttachment, XmATTACH_FORM,
					XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	t = XtVaCreateManagedWidget("title", xmLabelWidgetClass, detfr, XmNchildType, XmFRAME_TITLE_CHILD, NULL);
	set_label(t, "Details");
	det = XtVaCreateManagedWidget("details", xmFormWidgetClass, detfr, XmNwidth, 270, XmNmarginWidth, 10,
				      XmNmarginHeight, 10, XmNresizePolicy, XmRESIZE_NONE, NULL);
	d_name = XtVaCreateManagedWidget("name", xmLabelWidgetClass, det, XmNalignment, XmALIGNMENT_BEGINNING,
					 XmNrecomputeSize, False,
					 XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					 XmNrightAttachment, XmATTACH_FORM, NULL);
	if (big)
		XtVaSetValues(d_name, XmNrecomputeSize, True, XmNfontList, XmFontListCreate(big, XmFONTLIST_DEFAULT_TAG), NULL);
	set_label(d_name, "Software");		/* its height, then fixed */
	XtVaSetValues(d_name, XmNrecomputeSize, False, NULL);
	d_sub = field(det, d_name, 2);
	n = 0;
	XtSetArg(a[n], XmNeditable, False); n++;
	XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	XtSetArg(a[n], XmNwordWrap, True); n++;
	XtSetArg(a[n], XmNrows, 4); n++;
	XtSetArg(a[n], XmNcursorPositionVisible, False); n++;
	XtSetArg(a[n], XmNshadowThickness, 0); n++;
	XtSetArg(a[n], XmNtraversalOn, False); n++;
	d_desc = XmCreateText(det, "desc", a, n);
	XtVaSetValues(d_desc, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, d_sub, XmNtopOffset, 10,
		      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	XtManageChild(d_desc);
	{
		Pixel bg;

		XtVaGetValues(det, XmNbackground, &bg, NULL);
		XtVaSetValues(d_desc, XmNbackground, bg, NULL);
	}
	hdr = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, det, XmNtopAttachment, XmATTACH_WIDGET,
				      XmNtopWidget, d_desc, XmNtopOffset, 8, XmNleftAttachment, XmATTACH_FORM,
				      XmNrightAttachment, XmATTACH_FORM, NULL);
	d_status = field(det, hdr, 8);
	d_size = field(det, d_status, 4);
	d_needs = field(det, d_size, 4);
	d_neededby = field(det, d_needs, 4);
	d_twin = field(det, d_neededby, 8);
	b_main = XtVaCreateManagedWidget("main", xmPushButtonWidgetClass, det, XmNtopAttachment, XmATTACH_WIDGET,
					 XmNtopWidget, d_twin, XmNtopOffset, 14, XmNleftAttachment, XmATTACH_FORM,
					 XmNrightAttachment, XmATTACH_FORM, XmNmarginHeight, 6, XmNshowAsDefault, 1,
					 XmNrecomputeSize, False, NULL);
	XtAddCallback(b_main, XmNactivateCallback, act_cb, (XtPointer)A_NONE);
	b_try = XtVaCreateManagedWidget("try", xmPushButtonWidgetClass, det, XmNtopAttachment, XmATTACH_WIDGET,
					XmNtopWidget, b_main, XmNtopOffset, 6, XmNleftAttachment, XmATTACH_FORM,
					XmNrightAttachment, XmATTACH_FORM, NULL);
	set_label(b_try, "Try (dry run)");
	XtAddCallback(b_try, XmNactivateCallback, try_cb, NULL);

	pkgfr = XtVaCreateManagedWidget("pkgframe", xmFrameWidgetClass, work, XmNtopAttachment, XmATTACH_FORM,
					XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_WIDGET,
					XmNleftWidget, catfr, XmNleftOffset, 6, XmNrightAttachment, XmATTACH_WIDGET,
					XmNrightWidget, detfr, XmNrightOffset, 6, NULL);
	t = XtVaCreateManagedWidget("title", xmLabelWidgetClass, pkgfr, XmNchildType, XmFRAME_TITLE_CHILD, NULL);
	set_label(t, "   Name                   From Version     Status     Description");
	if (mono)
		XtVaSetValues(t, XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG), NULL);
	n = 0;
	XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
	XtSetArg(a[n], XmNvisibleItemCount, 18); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	XtSetArg(a[n], XmNwidth, 600); n++;
	XtSetArg(a[n], XmNlistSizePolicy, XmCONSTANT); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	l_pkgs = XmCreateScrolledList(pkgfr, "pkgs", a, n);
	XtManageChild(l_pkgs);
	XtAddCallback(l_pkgs, XmNbrowseSelectionCallback, pkgs_cb, NULL);
	XtAddCallback(l_pkgs, XmNdefaultActionCallback, pkgs_default_cb, NULL);

	make_progress();
	make_password();
	make_confirm();
	make_conflict();
	make_repos();
	make_prefs();

	reload();
	XtRealizeWidget(top);
	XmProcessTraversal(l_pkgs, XmTRAVERSE_CURRENT);
	if (cfg.update_at_start && (ips_ok || apkg_ok))
		XtAppAddTimeOut(app, 300, start_check_cb, NULL);
	XtAppMainLoop(app);
	return 0;
}
