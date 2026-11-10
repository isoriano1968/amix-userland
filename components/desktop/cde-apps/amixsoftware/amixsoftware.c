/*
 * amixsoftware.c -- the AMIX Software Center: a CDE window over apkg, the
 * Amix package client of the amigaux project (github.com/amigaux/amix,
 * userland/pkgtools).  For the people who install software with apkg.
 *
 * Categories on the left (All, Installed, Updates, then the catalog's own),
 * the packages in the middle, the chosen one's details on the right with
 * one clear button: Install, Upgrade or Remove, and "Try" beside it, which
 * runs the same thing as apkg -n (a dry run that changes nothing).
 *
 * It reads the catalog apkg keeps (/var/apkg/catalog, or where cachedir= in
 * /etc/apkg.conf puts it) and the installed packages (/var/sadm/pkg/<inst>
 * /pkginfo) itself, so browsing needs neither apkg nor root.  Changes run
 * apkg as a child, its output read through XtAppAddInput, a progress bar
 * from its "apkg: ... N/M bytes" lines.  root runs apkg directly; anyone
 * else gives root's password to the setuid helper swrun.
 *
 * The apkg program is a setting (~/.amixsoftware).  When it is not there a
 * warning bar says so and every change is off: apkg drives pkgadd and pkgrm
 * on the live system, and nothing else should.
 *
 * Every change is confirmed first, listing what comes with it, its size and
 * the free space on /; "Dry run only" is ticked by default (a setting).  An
 * install that would leave less than 200 MB free on / is refused.  Removing
 * a package of the original Amix 2.1 system asks twice.
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
#include <Xm/PanedW.h>

#define TITLE      "AMIX Software Center"
#define SWRUN      "/opt/amix/lib/amixsoftware/swrun"	/* AMIXSOFTWARE_SWRUN for tests */
#define DEF_APKG   "/usr/apkg/bin/apkg"
#define APKG_CONF  "/etc/apkg.conf"	/* AMIXSOFTWARE_APKGCONF for tests */
#define DEF_CATDIR "/var/apkg"
#define SADM       "/var/sadm/pkg"	/* AMIXSOFTWARE_SADM for tests */
#define MIN_FREE_KB (200L * 1024)
#define MAXPKG     512
#define MAXCAT     40

struct pkg {
	char name[40], version[40], inst[40], file[120];
	long size;
	char deps[256];			/* "a,b" or "" */
	char cat[32];
	char desc[256];
	int installed;			/* /var/sadm/pkg/<inst> is there */
	int original;			/* installed, without VERSION: Amix 2.1 */
	char iver[40];			/* the installed VERSION */
	int update;			/* the catalog's is newer */
};

static struct pkg pk[MAXPKG];
static int npk;
static char cats[MAXCAT][32];
static int ncats;
static int view[MAXPKG], nview;		/* list position -> pk index */
static int sel = -1;			/* pk index shown on the right */
static int cur_cat;			/* 0 All, 1 Installed, 2 Updates, 3+ cats[i-3] */

/* settings */
static char apkg_path[256] = DEF_APKG;
static int dry_default = 1, update_at_start;
static char repo[256] = "(not set)", catalog[320] = DEF_CATDIR "/catalog";
static int apkg_ok;
static char apkg_ver[64];
static time_t cat_time;

static XtAppContext app;
static Widget toolbar, searchlabel;
static Widget top, warnbar, warnlabel, l_cats, l_pkgs, t_search, l_status;
static Widget d_name, d_sub, d_desc, d_status, d_size, d_needs, d_neededby, b_main, b_try;
static Widget tb_update, tb_install, tb_upgrade, tb_remove;
static Widget m_install, m_upgrade, m_remove, m_try, m_update;
static XFontStruct *mono;

/* the running apkg */
static pid_t child = -1;
static int child_fd = -1;
static XtInputId child_in;
static char linebuf[1024];
static int linelen;
static Widget pg_dlg, pg_label, pg_bar, pg_text, pg_out, pg_toggle, pg_button;
static long pg_done, pg_total;
static Pixel bar_pixel;			/* the progress bar's fill */
static int pg_dry;
static char pg_what[200];

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
	if (bytes >= 1024L * 1024)
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

/* ------------------------------------------------------------ settings */

static char *settings_file(void)
{
	static char path[300];
	const char *h = getenv("HOME");

	sprintf(path, "%s/.amixsoftware", h && *h ? h : "/");
	return path;
}

static void load_settings(void)
{
	FILE *f = fopen(settings_file(), "r");
	char line[300];

	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		trim(line);
		if (!strncmp(line, "apkg=", 5) && line[5])
			copy(apkg_path, line + 5, sizeof apkg_path);
		else if (!strncmp(line, "dryrun=", 7))
			dry_default = atoi(line + 7) != 0;
		else if (!strncmp(line, "update_at_start=", 16))
			update_at_start = atoi(line + 16) != 0;
	}
	fclose(f);
}

static int save_settings(void)
{
	char tmp[310];
	FILE *f;

	sprintf(tmp, "%s.new", settings_file());
	if (!(f = fopen(tmp, "w")))
		return -1;
	fprintf(f, "# AMIX Software Center settings\napkg=%s\ndryrun=%d\nupdate_at_start=%d\n",
		apkg_path, dry_default, update_at_start);
	if (fclose(f) != 0)
		return -1;
	return rename(tmp, settings_file());
}

/* repo= and the catalog next to cachedir=, as apkg itself works them out */
static const char *apkg_conf(void)
{
	const char *e = getenv("AMIXSOFTWARE_APKGCONF");

	return e && *e ? e : APKG_CONF;
}

static const char *sadm(void)
{
	const char *e = getenv("AMIXSOFTWARE_SADM");

	return e && *e ? e : SADM;
}

static void load_apkg_conf(void)
{
	FILE *f = fopen(apkg_conf(), "r");
	char line[300], dir[300];

	strcpy(dir, DEF_CATDIR "/cache");
	if (f) {
		while (fgets(line, sizeof line, f)) {
			trim(line);
			if (!strncmp(line, "repo=", 5))
				copy(repo, line + 5, sizeof repo);
			else if (!strncmp(line, "cachedir=", 9) && line[9])
				copy(dir, line + 9, sizeof dir);
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

/* is apkg there, and which version: runs "apkg version" */
static int check_apkg(const char *path, char *ver)
{
	struct stat st;
	int p[2], n = 0, r, stt;
	pid_t pid;
	char buf[128];

	ver[0] = 0;
	if (path[0] != '/' || stat(path, &st) < 0 || !S_ISREG(st.st_mode) || access(path, X_OK) < 0)
		return 0;
	if (pipe(p) < 0)
		return 1;
	if ((pid = fork()) == 0) {
		int nul = open("/dev/null", O_RDWR);

		dup2(nul, 0);
		dup2(p[1], 1);
		dup2(nul, 2);
		close(p[0]);
		execl(path, path, "version", (char *)0);
		_exit(127);
	}
	close(p[1]);
	while (pid > 0 && n < (int)sizeof buf - 1 && (r = read(p[0], buf + n, sizeof buf - 1 - n)) > 0)
		n += r;
	close(p[0]);
	buf[n] = 0;
	if (pid > 0)
		waitpid(pid, &stt, 0);
	trim(buf);
	if (!strncmp(buf, "apkg ", 5)) {
		copy(ver, buf + 5, 64);
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------ data */

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

static void load_installed(void)
{
	int i;

	for (i = 0; i < npk; i++) {
		char path[200], line[200];
		FILE *f;

		pk[i].installed = pk[i].original = pk[i].update = 0;
		pk[i].iver[0] = 0;
		sprintf(path, "%s/%s/pkginfo", sadm(), pk[i].inst);
		if (!(f = fopen(path, "r")))
			continue;
		pk[i].installed = 1;
		while (fgets(line, sizeof line, f)) {
			trim(line);
			if (!strncmp(line, "VERSION=", 8))
				copy(pk[i].iver, line + 8, sizeof pk[i].iver);
		}
		fclose(f);
		if (!pk[i].iver[0])
			pk[i].original = 1;
		else if (vercmp(pk[i].iver, pk[i].version) < 0)
			pk[i].update = 1;
	}
}

static int cmp_cat(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

static int load_catalog(void)
{
	FILE *f = fopen(catalog, "r");
	char line[1200], *fl[10];
	struct stat st;
	int first = 1;

	npk = ncats = 0;
	cat_time = 0;
	if (!f)
		return -1;
	if (fstat(fileno(f), &st) == 0)
		cat_time = st.st_mtime;
	while (fgets(line, sizeof line, f) && npk < MAXPKG) {
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
		copy(pk[npk].name, fl[0], sizeof pk[0].name);
		copy(pk[npk].version, fl[1], sizeof pk[0].version);
		copy(pk[npk].inst, fl[2], sizeof pk[0].inst);
		copy(pk[npk].file, fl[3], sizeof pk[0].file);
		pk[npk].size = atol(fl[5]);
		copy(pk[npk].deps, strcmp(fl[6], "none") ? fl[6] : "", sizeof pk[0].deps);
		copy(pk[npk].cat, fl[7], sizeof pk[0].cat);
		copy(pk[npk].desc, fl[8], sizeof pk[0].desc);
		cat_index(pk[npk].cat);
		npk++;
	}
	fclose(f);
	qsort(cats, ncats, sizeof cats[0], cmp_cat);
	load_installed();
	return 0;
}

static int find_pkg(const char *name)
{
	int i;

	for (i = 0; i < npk; i++)
		if (!strcmp(pk[i].name, name))
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
	char d[40];
	int i;

	if (depth > 20)
		return;
	for (i = 0; i < *n; i++)
		if (out[i] == p)
			return;
	while (next_dep(&l, d, sizeof d)) {
		int q = find_pkg(d);

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
	char d[40];

	while (next_dep(&l, d, sizeof d))
		if (!strcmp(d, name))
			return 1;
	return 0;
}

/* ------------------------------------------------------------ the lists */

static int matches(const struct pkg *p, const char *q)
{
	char a[320], b[64];
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
	else
		out[0] = 0;
}

static void fill_cats(void)
{
	XmString *it = (XmString *)XtMalloc((ncats + 3) * sizeof(XmString));
	int i, j, ninst = 0, nupd = 0, n;
	char s[80];

	for (i = 0; i < npk; i++) {
		ninst += pk[i].installed;
		nupd += pk[i].update;
	}
	sprintf(s, "All  (%d)", npk);
	it[0] = XmStringCreateLocalized(s);
	sprintf(s, "Installed  (%d)", ninst);
	it[1] = XmStringCreateLocalized(s);
	sprintf(s, "Updates  (%d)", nupd);
	it[2] = XmStringCreateLocalized(s);
	for (i = 0; i < ncats; i++) {
		for (n = j = 0; j < npk; j++)
			n += !strcmp(pk[j].cat, cats[i]);
		sprintf(s, "%c%s  (%d)", toupper((unsigned char)cats[i][0]), cats[i] + 1, n);
		it[i + 3] = XmStringCreateLocalized(s);
	}
	XmListDeleteAllItems(l_cats);
	XmListAddItems(l_cats, it, ncats + 3, 0);
	for (i = 0; i < ncats + 3; i++)
		XmStringFree(it[i]);
	XtFree((char *)it);
	if (cur_cat >= ncats + 3)
		cur_cat = 0;
	XmListSelectPos(l_cats, cur_cat + 1, False);
}

static void show_details(void);

static void fill_pkgs(void)
{
	char *q = XmTextFieldGetString(t_search);
	XmString *it = (XmString *)XtMalloc((npk + 1) * sizeof(XmString));
	int i, keep = -1;
	char row[300], st[60];

	nview = 0;
	for (i = 0; i < npk; i++) {
		struct pkg *p = &pk[i];

		if (cur_cat == 1 && !p->installed)
			continue;
		if (cur_cat == 2 && !p->update)
			continue;
		if (cur_cat >= 3 && strcmp(p->cat, cats[cur_cat - 3]))
			continue;
		if (!matches(p, q))
			continue;
		state_word(p, st);
		sprintf(row, " %-15.15s %-12.12s %-10.10s %-.33s", p->name, p->version, st, p->desc);
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
	char s[700], when[40] = "never";
	int ninst = 0, i;

	for (i = 0; i < npk; i++)
		ninst += pk[i].installed;
	if (cat_time)
		strftime(when, sizeof when, "%d %b %H:%M", localtime(&cat_time));
	if (apkg_ok)
		sprintf(s, "%d packages, %d installed.   Catalog from %s.   apkg %s, %s", npk, ninst, when,
			apkg_ver, repo);
	else
		sprintf(s, "%d packages, %d installed.   Catalog from %s.   apkg not found", npk, ninst, when);
	status(s);
}

/* ------------------------------------------------------------ actions */

enum act { A_NONE, A_INSTALL, A_UPGRADE, A_REMOVE, A_UPDATE };

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

static void set_sensitivity(void)
{
	int busy = child > 0, ok = apkg_ok && !busy;
	enum act a = main_action(sel);

	XtSetSensitive(tb_update, ok);
	XtSetSensitive(m_update, ok);
	XtSetSensitive(tb_install, ok && a == A_INSTALL);
	XtSetSensitive(m_install, ok && a == A_INSTALL);
	XtSetSensitive(tb_upgrade, ok && a == A_UPGRADE);
	XtSetSensitive(m_upgrade, ok && a == A_UPGRADE);
	XtSetSensitive(tb_remove, ok && pk[sel < 0 ? 0 : sel].installed && sel >= 0);
	XtSetSensitive(m_remove, ok && pk[sel < 0 ? 0 : sel].installed && sel >= 0);
	XtSetSensitive(b_main, ok && a != A_NONE);
	XtSetSensitive(b_try, ok && a != A_NONE);
	XtSetSensitive(m_try, ok && a != A_NONE);
}

static void show_details(void)
{
	char s[400], sz[40], neededby[300];
	struct pkg *p;
	int i;
	enum act a;

	if (sel < 0) {
		set_label(d_name, npk ? "Nothing chosen" : "No catalog yet");
		set_label(d_sub, " ");
		XmTextSetString(d_desc, npk ? "Choose a package on the left."
					    : "Choose Update catalog to fetch the package list from the repository.");
		set_label(d_status, " ");
		set_label(d_size, " ");
		set_label(d_needs, " ");
		set_label(d_neededby, " ");
		set_label(b_main, "Install");
		set_sensitivity();
		return;
	}
	p = &pk[sel];
	set_label(d_name, p->name);
	sprintf(s, "%s  -  %s", p->version, p->cat);
	set_label(d_sub, s);
	XmTextSetString(d_desc, p->desc);
	if (p->update)
		sprintf(s, "Installed %s, %s available", p->iver, p->version);
	else if (p->original)
		strcpy(s, "Installed, part of the original Amix 2.1");
	else if (p->installed)
		sprintf(s, "Installed %s", p->iver);
	else
		strcpy(s, "Not installed");
	set_label(d_status, s);
	human(p->size, sz);
	sprintf(s, "Download %s", sz);
	set_label(d_size, s);
	if (p->deps[0]) {
		sprintf(s, "Needs %s", p->deps);
		for (i = 0; s[i]; i++)
			if (s[i] == ',')
				s[i] = ' ';
	} else
		strcpy(s, "Needs nothing else");
	set_label(d_needs, s);
	neededby[0] = 0;
	for (i = 0; i < npk; i++)
		if (pk[i].installed && has_dep(&pk[i], p->name) && strlen(neededby) < 250) {
			strcat(neededby, neededby[0] ? " " : "");
			strcat(neededby, pk[i].name);
		}
	if (neededby[0])
		sprintf(s, "Needed by %s", neededby);
	else
		strcpy(s, " ");
	set_label(d_neededby, s);
	a = main_action(sel);
	set_label(b_main, a == A_UPGRADE ? "Upgrade" : a == A_REMOVE ? "Remove" : "Install");
	set_sensitivity();
}

static void reload(void)
{
	char s[400];
	int r;

	load_apkg_conf();
	r = load_catalog();
	fill_cats();
	fill_pkgs();
	refresh_status();
	if (r == -2) {
		sprintf(s, "%s is not an apkg catalog (version 1).", catalog);
		message(s, 2);
	}
}

static void update_warning(void)
{
	char s[400];

	apkg_ok = check_apkg(apkg_path, apkg_ver);
	if (apkg_ok) {
		/* the toolbar moves up into the bar's place */
		XtVaSetValues(toolbar, XmNtopAttachment, XmATTACH_FORM, NULL);
		XtVaSetValues(t_search, XmNtopAttachment, XmATTACH_FORM, NULL);
		XtVaSetValues(searchlabel, XmNtopAttachment, XmATTACH_FORM, NULL);
		XtUnmanageChild(warnbar);
	} else {
		XtVaSetValues(toolbar, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, NULL);
		XtVaSetValues(t_search, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, NULL);
		XtVaSetValues(searchlabel, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar, NULL);
		sprintf(s, "apkg was not found at %s.\nInstall, upgrade and remove are off until it is: "
			   "only apkg may change the installed packages, or their records break.", apkg_path);
		set_label(warnlabel, s);
		XtManageChild(warnbar);
	}
	set_sensitivity();
	refresh_status();
}

/* ------------------------------------------------------------ running apkg */

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
	XtVaGetValues(pg_bar, XmNwidth, &w, XmNheight, &h, XmNforeground, &fg, XmNbackground, &bg, NULL);
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

static void pg_line(char *line)
{
	long a, b;
	char s[300];

	pg_append(line);
	pg_append("\n");
	if (sscanf(line, "apkg: ... %ld/%ld bytes", &a, &b) == 2 && b > 0) {
		char x[40], y[40];

		pg_done = a;
		pg_total = b;
		human(a, x);
		human(b, y);
		sprintf(s, "Downloading: %s of %s", x, y);
		set_label(pg_label, s);
		pg_draw();
	} else if (!strncmp(line, "apkg: fetch", 11) || !strncmp(line, "apkg: GET", 9)) {
		set_label(pg_label, "Downloading");
	} else if (strstr(line, "pkgadd") || strstr(line, "pkgrm")) {
		set_label(pg_label, strstr(line, "pkgrm") ? "Removing" : "Installing");
	}
}

static Widget pg_buttons;

static void show_output(int on)
{
	/* the buttons stay at the bottom; the output, when shown, fills the
	 * space between the toggle and them */
	if (on) {
		XtVaSetValues(pg_buttons, XmNtopAttachment, XmATTACH_NONE, NULL);
		XtManageChild(pg_out);
	} else {
		XtUnmanageChild(pg_out);
		XtVaSetValues(pg_buttons, XmNtopAttachment, XmATTACH_WIDGET,
			      XmNtopWidget, pg_toggle, NULL);
	}
	XmToggleButtonSetState(pg_toggle, on, False);
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
	if (code == 0) {
		if (pg_dry)
			strcpy(s, "Dry run finished.  Nothing was changed: the output shows what apkg would do.");
		else
			sprintf(s, "%s: done.", pg_what);
		pg_total = pg_total ? pg_total : 1;
		pg_done = pg_total;
		pg_draw();
	} else if (code == 3) {
		strcpy(s, "Wrong root password: nothing was changed.");
		show_output(1);
	} else {
		if (WIFSIGNALED(st))
			sprintf(s, "Stopped: apkg ended on signal %d.  See the output.", WTERMSIG(st));
		else
			sprintf(s, "apkg stopped with an error (exit %d).  See the output.", code);
		show_output(1);
	}
	set_label(pg_label, s);
	set_label(pg_button, "Close");
	reload();
	update_warning();
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

static void pg_button_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (child > 0) {
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
	Widget f, bar;
	Arg a[8];
	int n = 0;

	pg_dlg = XmCreateFormDialog(top, "progress", NULL, 0);
	XtVaSetValues(pg_dlg, XmNmarginWidth, 12, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = pg_dlg;
	pg_label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					   XmNrightAttachment, XmATTACH_FORM, NULL);
	pg_bar = XtVaCreateManagedWidget("bar", xmDrawingAreaWidgetClass, f, XmNheight, 14, XmNwidth, 460,
					 XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_label, XmNtopOffset, 8,
					 XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	XtAddCallback(pg_bar, XmNexposeCallback, pg_expose, NULL);
	pg_toggle = XtVaCreateManagedWidget("toggle", xmToggleButtonWidgetClass, f,
					    XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_bar, XmNtopOffset, 8,
					    XmNleftAttachment, XmATTACH_FORM, NULL);
	set_label(pg_toggle, "Show apkg output");
	XtVaSetValues(pg_toggle, XmNselectColor, bar_pixel, NULL);
	XtAddCallback(pg_toggle, XmNvalueChangedCallback, pg_toggle_cb, NULL);
	bar = pg_buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pg_toggle, XmNtopOffset, 10,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	pg_button = XtVaCreateManagedWidget("stop", xmPushButtonWidgetClass, bar, NULL);
	XtAddCallback(pg_button, XmNactivateCallback, pg_button_cb, NULL);
	XtSetArg(a[n], XmNeditable, False); n++;
	XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	XtSetArg(a[n], XmNrows, 12); n++;
	XtSetArg(a[n], XmNcolumns, 72); n++;
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

/* start apkg [-n] verb names...; with the password when we are not root */
static void run_apkg(const char *title, int dry, const char *verb, int *list, int nlist, const char *password)
{
	char *argv[64], target[300];
	int n = 0, i, p[2], in[2] = { -1, -1 }, root = getuid() == 0;

	if (root) {
		argv[n++] = apkg_path;
	} else {
		int l = readlink(apkg_path, target, sizeof target - 1);

		const char *e = getenv("AMIXSOFTWARE_SWRUN");

		argv[n++] = e && *e ? (char *)e : SWRUN;
		if (l > 0 && target[0] == '/') {	/* swrun takes no links */
			target[l] = 0;
			argv[n++] = target;
		} else
			argv[n++] = apkg_path;
	}
	if (dry)
		argv[n++] = "-n";
	argv[n++] = (char *)verb;
	for (i = 0; i < nlist && n < 60; i++)
		argv[n++] = pk[list[i]].name;
	argv[n] = NULL;
	if (pipe(p) < 0 || (!root && pipe(in) < 0)) {
		message("Cannot start apkg: no pipe.", 2);
		return;
	}
	pg_dry = dry;
	copy(pg_what, title, sizeof pg_what);
	pg_done = pg_total = 0;
	linelen = 0;
	XmTextSetString(pg_text, "");
	XtVaSetValues(XtParent(pg_dlg), XmNtitle, title, NULL);
	set_label(pg_label, dry ? "Starting a dry run" : "Starting");
	set_label(pg_button, "Stop");
	show_output(dry);
	{
		char cmd[600] = "$";

		for (i = 0; argv[i]; i++) {
			strcat(cmd, " ");
			strncat(cmd, argv[i], 590 - strlen(cmd));
		}
		pg_append(cmd);
		pg_append("\n");
	}
	if ((child = fork()) == 0) {
		int nul = open("/dev/null", O_RDONLY);

		setsid();
		if (root) {
			dup2(nul, 0);
		} else {
			dup2(in[0], 0);
			close(in[1]);
		}
		dup2(p[1], 1);
		dup2(p[1], 2);
		close(p[0]);
		execv(argv[0], argv);
		fprintf(stderr, "cannot run %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	close(p[1]);
	if (!root) {
		close(in[0]);
		if (child > 0 && password)
			write(in[1], password, strlen(password));
		write(in[1], "\n", 1);
		close(in[1]);
	}
	if (child < 0) {
		close(p[0]);
		message("Cannot start apkg: no process.", 2);
		return;
	}
	child_fd = p[0];
	child_in = XtAppAddInput(app, child_fd, (XtPointer)XtInputReadMask, child_input, NULL);
	XtManageChild(pg_dlg);
	set_sensitivity();
}

/* ------------------------------------------------------------ password */

static Widget pw_dlg, pw_text, pw_label;
static char pw_buf[128];
static int pw_len;
static struct pending {
	char title[200], verb[16];
	int dry, list[64], n;
} pend;

static void pw_modify(Widget w, XtPointer cd, XtPointer cb)
{
	XmTextVerifyCallbackStruct *v = (XmTextVerifyCallbackStruct *)cb;
	int i, del = v->endPos - v->startPos;

	if (v->startPos < pw_len && del > 0) {	/* only deleting at the end makes sense */
		pw_len = v->startPos;
	}
	for (i = 0; v->text && i < v->text->length && pw_len < (int)sizeof pw_buf - 1; i++) {
		pw_buf[pw_len++] = v->text->ptr[i];
		v->text->ptr[i] = '*';
	}
	pw_buf[pw_len] = 0;
}

static void pw_ok(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(pw_dlg);
	run_apkg(pend.title, pend.dry, pend.verb, pend.list, pend.n, pw_buf);
	memset(pw_buf, 0, sizeof pw_buf);
	pw_len = 0;
	XmTextFieldSetString(pw_text, "");
}

static void pw_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(pw_dlg);
	memset(pw_buf, 0, sizeof pw_buf);
	pw_len = 0;
	XmTextFieldSetString(pw_text, "");
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

static void start(const char *title, int dry, const char *verb, int *list, int n)
{
	char s[300];

	if (getuid() == 0) {
		run_apkg(title, dry, verb, list, n, NULL);
		return;
	}
	copy(pend.title, title, sizeof pend.title);
	copy(pend.verb, verb, sizeof pend.verb);
	pend.dry = dry;
	pend.n = n > 64 ? 64 : n;
	memcpy(pend.list, list, pend.n * sizeof(int));
	sprintf(s, "%s needs the root password.%s", title,
		dry ? "\nA dry run changes nothing, but apkg still runs as root." : "");
	set_label(pw_label, s);
	XtManageChild(pw_dlg);
	XmProcessTraversal(pw_text, XmTRAVERSE_CURRENT);
}

/* ------------------------------------------------------------ confirm */

static Widget cf_dlg, cf_label, cf_list, cf_total, cf_dry, cf_ok;
static int cf_pk[64], cf_n;
static enum act cf_act;
static int cf_second;			/* removing an Amix 2.1 package: asked again */

static void cf_update_button(void)
{
	int dry = XmToggleButtonGetState(cf_dry);

	set_label(cf_ok, dry ? "Try" : cf_act == A_REMOVE ? "Remove" : cf_act == A_UPGRADE ? "Upgrade" : "Install");
}

static void cf_dry_cb(Widget w, XtPointer cd, XtPointer cb)
{
	cf_update_button();
}

static void cf_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(cf_dlg);
}

static const char *verb_of(enum act a)
{
	return a == A_REMOVE ? "remove" : a == A_UPGRADE ? "upgrade" : "install";
}

static void original_confirmed(Widget w, XtPointer cd, XtPointer cb)
{
	char title[200];

	XtDestroyWidget(XtParent(w));
	sprintf(title, "Removing %s", pk[cf_pk[0]].name);
	start(title, 0, "remove", cf_pk, 1);
}

static void cf_ok_cb(Widget w, XtPointer cd, XtPointer cb)
{
	int dry = XmToggleButtonGetState(cf_dry), i;
	long need = 0, fr = free_kb("/");
	char title[200], s[500];

	XtUnmanageChild(cf_dlg);
	if (!dry && cf_act != A_REMOVE) {
		for (i = 0; i < cf_n; i++)
			need += pk[cf_pk[i]].size;
		/* the download and the installed files: about twice the package */
		if (fr >= 0 && fr - 2 * need / 1024 < MIN_FREE_KB) {
			sprintf(s, "Not enough room on /: %ld MB free, and at least 200 MB must stay free after "
				   "the install.  Make room first, or use Try to see what would happen.",
				fr / 1024);
			message(s, 2);
			return;
		}
	}
	if (cf_act == A_REMOVE && !dry && pk[cf_pk[0]].original) {
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
	if (cf_n > 1)
		sprintf(title, "%s %s and %d more", cf_act == A_REMOVE ? "Removing" : cf_act == A_UPGRADE ? "Upgrading" : "Installing",
			pk[cf_pk[cf_n - 1]].name, cf_n - 1);
	else
		sprintf(title, "%s %s", cf_act == A_REMOVE ? "Removing" : cf_act == A_UPGRADE ? "Upgrading" : "Installing",
			pk[cf_pk[0]].name);
	if (dry)
		strcat(title, " (dry run)");
	/* apkg install resolves the dependencies itself: name only the package */
	start(title, dry, verb_of(cf_act), &cf_pk[cf_n - 1], 1);
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
	XtSetArg(a[n], XmNvisibleItemCount, 5); n++;
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
	set_label(cf_dry, "Dry run only (apkg -n: shows what would happen, changes nothing)");
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

static void confirm(enum act a, int dry)
{
	XmString it[64];
	char s[300], sz[40];
	long total = 0, fr;
	int i;

	if (sel < 0)
		return;
	if (a == A_REMOVE && (!strcmp(pk[sel].name, "apkg") || !strcmp(pk[sel].name, "apkgeng"))) {
		message("The Software Center cannot remove apkg or its engine: it works through them.\n"
			"To remove them, use pkgrm as root in a terminal.", 1);
		return;
	}
	cf_act = a;
	cf_n = 0;
	if (a == A_INSTALL)
		resolve(sel, cf_pk, &cf_n, 0);
	else
		cf_pk[cf_n++] = sel;
	for (i = 0; i < cf_n; i++) {
		struct pkg *p = &pk[cf_pk[i]];

		human(p->size, sz);
		if (a == A_REMOVE)
			sprintf(s, " %-15.15s %s", p->name, p->original ? "Amix 2.1" : p->iver);
		else
			sprintf(s, " %-15.15s %-12.12s %10s  %s", p->name, p->version, sz,
				cf_pk[i] == sel ? "" : "needed");
		it[i] = XmStringCreateLocalized(s);
		total += p->size;
	}
	XmListDeleteAllItems(cf_list);
	XmListAddItems(cf_list, it, cf_n, 0);
	for (i = 0; i < cf_n; i++)
		XmStringFree(it[i]);
	if (a == A_REMOVE)
		sprintf(s, "Remove %s from this system?", pk[sel].name);
	else if (a == A_UPGRADE)
		sprintf(s, "Upgrade %s from %s to %s?", pk[sel].name, pk[sel].iver, pk[sel].version);
	else if (cf_n > 1)
		sprintf(s, "Install %s and the %d package%s it needs?", pk[sel].name, cf_n - 1, cf_n > 2 ? "s" : "");
	else
		sprintf(s, "Install %s?", pk[sel].name);
	set_label(cf_label, s);
	fr = free_kb("/");
	human(total, sz);
	if (a == A_REMOVE) {
		char by[300] = "";

		for (i = 0; i < npk; i++)
			if (pk[i].installed && has_dep(&pk[i], pk[sel].name) && strlen(by) < 250) {
				strcat(by, by[0] ? ", " : "");
				strcat(by, pk[i].name);
			}
		if (by[0])
			sprintf(s, "Needed by %s: they may stop working.", by);
		else
			strcpy(s, "No installed package needs it.");
	}
	else
		sprintf(s, "Download: %s        Free on /: %ld MB", sz, fr / 1024);
	set_label(cf_total, s);
	XmToggleButtonSetState(cf_dry, dry, False);
	cf_update_button();
	XtVaSetValues(XtParent(cf_dlg), XmNtitle, a == A_REMOVE ? "Remove package" : a == A_UPGRADE ? "Upgrade package" : "Install packages", NULL);
	XtManageChild(cf_dlg);
}

static void act_cb(Widget w, XtPointer cd, XtPointer cb)
{
	enum act a = (enum act)(long)cd;

	if (!apkg_ok || child > 0)
		return;
	if (a == A_NONE)
		a = main_action(sel);
	confirm(a, dry_default);
}

static void try_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (!apkg_ok || child > 0 || sel < 0)
		return;
	confirm(main_action(sel), 1);
}

static void update_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (!apkg_ok || child > 0)
		return;
	start("Updating the catalog", 0, "update", NULL, 0);
}

/* ------------------------------------------------------------ settings dialog */

static Widget st_dlg, st_path, st_found, st_dry, st_upd, st_repo, fsb;

static void st_check(void)
{
	char *p = XmTextFieldGetString(st_path), v[64], s[300];

	if (check_apkg(p, v))
		sprintf(s, "Found: apkg %s", v);
	else
		sprintf(s, "Not found: no apkg program at %s", p);
	set_label(st_found, s);
	XtFree(p);
}

static void st_changed(Widget w, XtPointer cd, XtPointer cb)
{
	st_check();
}

static void fsb_ok(Widget w, XtPointer cd, XtPointer cb)
{
	XmFileSelectionBoxCallbackStruct *c = (XmFileSelectionBoxCallbackStruct *)cb;
	char *f;

	if (XmStringGetLtoR(c->value, XmFONTLIST_DEFAULT_TAG, &f)) {
		XmTextFieldSetString(st_path, f);
		XtFree(f);
	}
	XtUnmanageChild(w);
	st_check();
}

static void fsb_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(w);
}

static void st_browse(Widget w, XtPointer cd, XtPointer cb)
{
	if (!fsb) {
		XmString d = XmStringCreateLocalized("/usr/apkg/bin");

		fsb = XmCreateFileSelectionDialog(st_dlg, "choose", NULL, 0);
		XtVaSetValues(fsb, XmNdirectory, d, NULL);
		XmStringFree(d);
		XtVaSetValues(XtParent(fsb), XmNtitle, "Choose the apkg program", NULL);
		XtAddCallback(fsb, XmNokCallback, fsb_ok, NULL);
		XtAddCallback(fsb, XmNcancelCallback, fsb_cancel, NULL);
		XtUnmanageChild(XmFileSelectionBoxGetChild(fsb, XmDIALOG_HELP_BUTTON));
	}
	XtManageChild(fsb);
}

static void st_default(Widget w, XtPointer cd, XtPointer cb)
{
	XmTextFieldSetString(st_path, DEF_APKG);
	st_check();
}

static void st_save(Widget w, XtPointer cd, XtPointer cb)
{
	char *p = XmTextFieldGetString(st_path);

	copy(apkg_path, p, sizeof apkg_path);
	XtFree(p);
	dry_default = XmToggleButtonGetState(st_dry);
	update_at_start = XmToggleButtonGetState(st_upd);
	if (save_settings() < 0)
		message("The settings could not be saved in your home directory.", 2);
	XtUnmanageChild(st_dlg);
	update_warning();
}

static void st_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(st_dlg);
}

static Widget heading(Widget f, Widget above, const char *text)
{
	Widget l = XtVaCreateManagedWidget("heading", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM, XmNtopWidget, above,
					   XmNtopOffset, above ? 14 : 0, XmNleftAttachment, XmATTACH_FORM, NULL);
	set_label(l, text);
	return l;
}

static void make_settings(void)
{
	Widget f, h1, row, b, h2, h3, bar, note;

	st_dlg = XmCreateFormDialog(top, "settings", NULL, 0);
	XtVaSetValues(XtParent(st_dlg), XmNtitle, "Settings", NULL);
	XtVaSetValues(st_dlg, XmNmarginWidth, 14, XmNmarginHeight, 12, XmNautoUnmanage, False, NULL);
	f = st_dlg;
	h1 = heading(f, NULL, "The apkg program");
	row = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, h1, XmNtopOffset, 4,
				      XmNleftAttachment, XmATTACH_FORM, NULL);
	st_path = XtVaCreateManagedWidget("path", xmTextFieldWidgetClass, row, XmNcolumns, 34, NULL);
	XtAddCallback(st_path, XmNactivateCallback, st_changed, NULL);
	XtAddCallback(st_path, XmNlosingFocusCallback, st_changed, NULL);
	b = XtVaCreateManagedWidget("browse", xmPushButtonWidgetClass, row, NULL);
	set_label(b, "Choose...");
	XtAddCallback(b, XmNactivateCallback, st_browse, NULL);
	b = XtVaCreateManagedWidget("default", xmPushButtonWidgetClass, row, NULL);
	set_label(b, "Default");
	XtAddCallback(b, XmNactivateCallback, st_default, NULL);
	st_found = XtVaCreateManagedWidget("found", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, row, XmNtopOffset, 4,
					   XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	h2 = heading(f, st_found, "Repository");
	st_repo = XtVaCreateManagedWidget("repo", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, h2, XmNtopOffset, 4,
					  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	h3 = heading(f, st_repo, "Safety");
	st_dry = XtVaCreateManagedWidget("dry", xmToggleButtonWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
					 XmNtopWidget, h3, XmNtopOffset, 2, XmNleftAttachment, XmATTACH_FORM, NULL);
	XtVaSetValues(st_dry, XmNselectColor, bar_pixel, NULL);
	set_label(st_dry, "Tick \"Dry run only\" in every confirmation");
	st_upd = XtVaCreateManagedWidget("upd", xmToggleButtonWidgetClass, f, XmNtopAttachment, XmATTACH_WIDGET,
					 XmNtopWidget, st_dry, XmNleftAttachment, XmATTACH_FORM, NULL);
	XtVaSetValues(st_upd, XmNselectColor, bar_pixel, NULL);
	set_label(st_upd, "Update the catalog when the Software Center starts");
	note = XtVaCreateManagedWidget("note", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
				       XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, st_upd, XmNtopOffset, 10,
				       XmNleftAttachment, XmATTACH_FORM, NULL);
	set_label(note, "These settings are yours (~/.amixsoftware).  The repository is apkg's own,\n"
			"set in " APKG_CONF ".");
	bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, note, XmNtopOffset, 12,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	b = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, bar, NULL);
	set_label(b, "Cancel");
	XtAddCallback(b, XmNactivateCallback, st_cancel, NULL);
	b = XtVaCreateManagedWidget("save", xmPushButtonWidgetClass, bar, XmNshowAsDefault, True, NULL);
	set_label(b, "Save");
	XtAddCallback(b, XmNactivateCallback, st_save, NULL);
}

static void settings_cb(Widget w, XtPointer cd, XtPointer cb)
{
	char s[700];

	XmTextFieldSetString(st_path, apkg_path);
	XmToggleButtonSetState(st_dry, dry_default, False);
	XmToggleButtonSetState(st_upd, update_at_start, False);
	sprintf(s, "%s   (catalog %s)", repo, catalog);
	set_label(st_repo, s);
	st_check();
	XtManageChild(st_dlg);
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

static void quit_cb(Widget w, XtPointer cd, XtPointer cb)
{
	if (child > 0) {
		message("apkg is still working.  Wait for it, or stop it in its window, before closing.", 1);
		return;
	}
	exit(0);
}

static void refresh_cb(Widget w, XtPointer cd, XtPointer cb)
{
	reload();
	update_warning();
}

static void about_cb(Widget w, XtPointer cd, XtPointer cb)
{
	message(TITLE "\n\nFinds, installs, upgrades and removes Amix software through apkg,\n"
		"the package client of the amigaux project (pkg.amigaux.org).\n\n"
		"Browsing needs nothing special.  Changes need apkg and the root password;\n"
		"Try runs a dry run (apkg -n) that shows what would happen and changes nothing.",
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

static void start_update_cb(XtPointer cd, XtIntervalId *id)
{
	update_cb(NULL, NULL, NULL);
}

int main(int argc, char **argv)
{
	/* bitmap fonts by their full names: a wildcard pattern can get a
	 * scaled instance (avg width 0, other resolution), which X11R6.3
	 * renders far slower on the 060.  The UI fonts are CDE's own. */
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
	top = XtVaOpenApplication(&app, "Amixsoftware", NULL, 0, &argc, argv, NULL,
				  sessionShellWidgetClass, XmNtitle, TITLE, XmNiconName, "Software", NULL);
	if (argc > 1) {
		fprintf(stderr, "usage: amixsoftware\n");
		return 2;
	}
	mono = load_font(XtDisplay(top), monos);
	big = load_font(XtDisplay(top), bigs);
	load_settings();

	mw = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, top, NULL);
	mbar = XmCreateMenuBar(mw, "menubar", NULL, 0);
	pd = menu(mbar, "File", 'F', 0);
	m_update = item(pd, "Update catalog", update_cb, NULL);
	item(pd, "Reload", refresh_cb, NULL);
	item(pd, "Settings...", settings_cb, NULL);
	XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, pd, NULL);
	item(pd, "Close", quit_cb, NULL);
	pd = menu(mbar, "Package", 'P', 0);
	m_install = item(pd, "Install...", act_cb, (XtPointer)A_INSTALL);
	m_upgrade = item(pd, "Upgrade...", act_cb, (XtPointer)A_UPGRADE);
	m_remove = item(pd, "Remove...", act_cb, (XtPointer)A_REMOVE);
	XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, pd, NULL);
	m_try = item(pd, "Try (dry run)...", try_cb, NULL);
	pd = menu(mbar, "Help", 'H', 1);
	item(pd, "About the Software Center", about_cb, NULL);
	XtManageChild(mbar);

	form = XtVaCreateManagedWidget("form", xmFormWidgetClass, mw, XmNmarginWidth, 8, XmNmarginHeight, 6, NULL);

	/* the warning bar: shown only while apkg is missing */
	warnbar = XtVaCreateWidget("warnbar", xmFormWidgetClass, form, XmNtopAttachment, XmATTACH_FORM,
				   XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
				   XmNshadowType, XmSHADOW_OUT, XmNshadowThickness, 1,
				   XmNmarginWidth, 8, XmNmarginHeight, 6,
				   XtVaTypedArg, XmNbackground, XmRString, "#f2d675", 8, NULL);
	t = XtVaCreateManagedWidget("setpath", xmPushButtonWidgetClass, warnbar, XmNrightAttachment, XmATTACH_FORM,
				    XmNtopAttachment, XmATTACH_FORM, NULL);
	set_label(t, "Set path...");
	XtAddCallback(t, XmNactivateCallback, settings_cb, NULL);
	warnlabel = XtVaCreateManagedWidget("warning", xmLabelWidgetClass, warnbar, XmNalignment, XmALIGNMENT_BEGINNING,
					    XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
					    XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_WIDGET,
					    XmNrightWidget, t, XmNrightOffset, 10,
					    XtVaTypedArg, XmNbackground, XmRString, "#f2d675", 8,
					    XtVaTypedArg, XmNforeground, XmRString, "#4a3200", 8, NULL);

	toolbar = XtVaCreateManagedWidget("toolbar", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
					  XmNspacing, 6, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, warnbar,
					  XmNtopOffset, 6, XmNleftAttachment, XmATTACH_FORM, NULL);
	tb_update = tool(toolbar, "Update catalog", update_cb, NULL);
	tb_install = tool(toolbar, "Install", act_cb, (XtPointer)A_INSTALL);
	tb_upgrade = tool(toolbar, "Upgrade", act_cb, (XtPointer)A_UPGRADE);
	tb_remove = tool(toolbar, "Remove", act_cb, (XtPointer)A_REMOVE);
	t_search = XtVaCreateManagedWidget("search", xmTextFieldWidgetClass, form, XmNcolumns, 22,
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
	det = XtVaCreateManagedWidget("details", xmFormWidgetClass, detfr, XmNwidth, 250, XmNmarginWidth, 10,
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
	b_main = XtVaCreateManagedWidget("main", xmPushButtonWidgetClass, det, XmNtopAttachment, XmATTACH_WIDGET,
					 XmNtopWidget, d_neededby, XmNtopOffset, 16, XmNleftAttachment, XmATTACH_FORM,
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
	set_label(t, "Name            Version      Status     Description");
	if (mono)
		XtVaSetValues(t, XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG), NULL);
	n = 0;
	XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
	XtSetArg(a[n], XmNvisibleItemCount, 18); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	XtSetArg(a[n], XmNwidth, 560); n++;
	XtSetArg(a[n], XmNlistSizePolicy, XmCONSTANT); n++;
	if (mono) {
		XtSetArg(a[n], XmNfontList, XmFontListCreate(mono, XmFONTLIST_DEFAULT_TAG)); n++;
	}
	l_pkgs = XmCreateScrolledList(pkgfr, "pkgs", a, n);
	XtManageChild(l_pkgs);
	XtAddCallback(l_pkgs, XmNbrowseSelectionCallback, pkgs_cb, NULL);
	XtAddCallback(l_pkgs, XmNdefaultActionCallback, pkgs_default_cb, NULL);

	{
		XColor c, exact;
		Colormap cm = DefaultColormapOfScreen(XtScreen(top));

		if (XAllocNamedColor(XtDisplay(top), cm, "#3a6ea5", &c, &exact))
			bar_pixel = c.pixel;
		else
			bar_pixel = BlackPixelOfScreen(XtScreen(top));
	}
	make_progress();
	make_password();
	make_confirm();
	make_settings();

	reload();
	update_warning();
	XtRealizeWidget(top);
	XmProcessTraversal(l_pkgs, XmTRAVERSE_CURRENT);
	if (update_at_start && apkg_ok)
		XtAppAddTimeOut(app, 300, start_update_cb, NULL);
	XtAppMainLoop(app);
	return 0;
}
