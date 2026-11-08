/*
 * amixprinters.c -- Printers for AMIX in CDE: find the network printers
 * (UPnP and Bonjour, through amixprfind), add one to the lp spooler, make
 * it the default, set its paper, sides and colour, print a test page,
 * remove it.
 *
 * The search runs as a child (amixprfind, a few seconds of broadcasts) whose
 * output comes in through XtAppAddInput, so the window never blocks.  The
 * changes to lp go through the setuid helper prset, which only knows add,
 * default, remove (its own printers only) and test.
 *
 * A printer is added for its raw TCP port (9100 style, Bonjour's
 * _pdl-datastream or a UPnP printer): the lp interface sends every file
 * unchanged, so the printer must take PostScript itself (and PDF or PCL,
 * when the files are such).  The languages it names are shown.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/TextF.h>
#include <Xm/RowColumn.h>
#include <Xm/MessageB.h>

#define LIBDIR  "/opt/amix/lib/amixprinters"
#define FIND    LIBDIR "/amixprfind"
#define PRSET   LIBDIR "/prset"
#define OPTDIR  "/etc/lp/amixprinters"
#define MAXF    16
#define MAXL    16
#define S       128

struct found {				/* one printer: the lines of one address */
	char ip[16];
	int raw;			/* its raw port, 0 if none */
	char name[S], model[S], pdl[256], admin[S];
	char protos[40];		/* "raw 9100, IPP, LPD" */
};

struct local {				/* a destination lp knows */
	char name[20];
	char desc[S];
	int dflt;
};

static XtAppContext app;
static Widget top, l_found, l_local, t_name, b_add, b_search, b_default, b_test, b_remove, b_props, l_status, l_detail;
static Widget propdlg, o_paper, o_sides, o_color, l_propname;
static struct found fd_[MAXF];
static int nfound, sel_found = -1;
static struct local lc[MAXL];
static int nlocal, sel_local = -1;

static int find_fd = -1;
static pid_t find_pid;
static XtInputId find_in;
static char find_buf[8192];
static int find_len;

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
	XtDestroyWidget(w);
}

static void message(const char *text, int error)
{
	Widget d = error ? XmCreateErrorDialog(top, "error", NULL, 0)
			 : XmCreateInformationDialog(top, "info", NULL, 0);
	XmString x = XmStringCreateLtoR((char *)text, XmFONTLIST_DEFAULT_TAG);

	XtVaSetValues(d, XmNmessageString, x, NULL);
	XtVaSetValues(XtParent(d), XmNtitle, "Printers", NULL);
	XmStringFree(x);
	XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
	XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
	XtAddCallback(d, XmNokCallback, destroy_cb, NULL);
	XtManageChild(d);
}

/* does the comma list pdl hold exactly this entry (case blind)? */
static int has(const char *pdl, const char *entry)
{
	int l = strlen(entry);

	while (*pdl) {
		if (!strncasecmp(pdl, entry, l) && (pdl[l] == ',' || !pdl[l]))
			return 1;
		while (*pdl && *pdl != ',')
			pdl++;
		if (*pdl == ',')
			pdl++;
	}
	return 0;
}

/* "application/postscript,application/pdf,..." -> "PostScript, PDF, PCL" */
static void languages(const char *pdl, char *out)
{
	out[0] = 0;
	if (has(pdl, "application/postscript"))
		strcat(out, "PostScript, ");
	if (has(pdl, "application/pdf"))
		strcat(out, "PDF, ");
	if (has(pdl, "application/vnd.hp-PCL"))
		strcat(out, "PCL, ");
	if (has(pdl, "application/vnd.hp-PCLXL"))
		strcat(out, "PCL XL, ");
	if (has(pdl, "image/pwg-raster") || has(pdl, "image/urf"))
		strcat(out, "raster, ");
	if (out[0])
		out[strlen(out) - 2] = 0;
	else
		strcpy(out, "not named");
}

/* ------------------------------------------------------------ lp state */

static int run_out(char *const argv[], char *out, int outlen)
{
	int p[2], n = 0, r, st;
	pid_t pid;

	if (pipe(p) < 0)
		return -1;
	if ((pid = fork()) < 0)
		return -1;
	if (pid == 0) {
		close(p[0]);
		dup2(p[1], 1);
		dup2(p[1], 2);
		execv(argv[0], argv);
		_exit(127);
	}
	close(p[1]);
	while (n < outlen - 1 && (r = read(p[0], out + n, outlen - 1 - n)) > 0)
		n += r;
	out[n] = 0;
	close(p[0]);
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void load_local(void)
{
	static char *pd[] = { "/usr/bin/lpstat", "-p", "-D", NULL };
	static char *dd[] = { "/usr/bin/lpstat", "-d", NULL };
	char out[8192], def[40] = "", *l, *e;
	int i;

	nlocal = 0;
	if (run_out(dd, out, sizeof out) >= 0 && (l = strstr(out, "destination: "))) {
		sscanf(l + 13, "%39s", def);
	}
	if (run_out(pd, out, sizeof out) < 0)
		return;
	for (l = out; *l && nlocal < MAXL; l = e) {
		e = strchr(l, '\n');
		e = e ? e + 1 : l + strlen(l);
		if (!strncmp(l, "printer ", 8)) {
			memset(&lc[nlocal], 0, sizeof lc[nlocal]);
			sscanf(l + 8, "%19s", lc[nlocal].name);
			lc[nlocal].dflt = !strcmp(lc[nlocal].name, def);
			nlocal++;
		} else if (nlocal && strstr(l, "Description:")) {
			char *d = strstr(l, "Description:") + 12;
			int k = 0;

			while (*d == ' ')
				d++;
			while (d[k] && d[k] != '\n' && k < S - 1) {
				lc[nlocal - 1].desc[k] = d[k];
				k++;
			}
			lc[nlocal - 1].desc[k] = 0;
		}
	}
	(void)i;
}

static void fill_local(void)
{
	int i;
	char s[300];
	XmString x;

	XmListDeleteAllItems(l_local);
	for (i = 0; i < nlocal; i++) {
		sprintf(s, "%-12s %s%s", lc[i].name, lc[i].dflt ? "(default)  " : "", lc[i].desc);
		x = XmStringCreateLocalized(s);
		XmListAddItem(l_local, x, 0);
		XmStringFree(x);
	}
	if (!nlocal) {
		x = XmStringCreateLocalized("(no printers set up: add one from the list above)");
		XmListAddItem(l_local, x, 0);
		XmStringFree(x);
	}
	sel_local = -1;
	XtSetSensitive(b_default, False);
	XtSetSensitive(b_test, False);
	XtSetSensitive(b_props, False);
	XtSetSensitive(b_remove, False);
}

/* ------------------------------------------------------------ search */

static void add_line(char *line)
{
	char *f[7];
	int i, k;
	struct found *p = NULL;

	for (i = 0; i < 7; i++) {
		f[i] = line;
		if (i < 6) {
			if (!(line = strchr(line, '\t')))
				return;
			*line++ = 0;
		}
	}
	for (k = 0; k < nfound; k++)
		if (!strcmp(fd_[k].ip, f[0]))
			p = &fd_[k];
	if (!p) {
		if (nfound == MAXF)
			return;
		p = &fd_[nfound++];
		memset(p, 0, sizeof *p);
		strncpy(p->ip, f[0], 15);
	}
	if (!p->name[0] || !strcmp(f[2], "raw"))
		strncpy(p->name, f[3], S - 1);
	if (strcmp(f[4], "-") && !p->model[0])
		strncpy(p->model, f[4], S - 1);
	if (strcmp(f[5], "-") && strlen(f[5]) > strlen(p->pdl))
		strncpy(p->pdl, f[5], sizeof p->pdl - 1);
	if (strcmp(f[6], "-") && !p->admin[0])
		strncpy(p->admin, f[6], S - 1);
	if (!strcmp(f[2], "raw") || (!strcmp(f[2], "upnp") && !p->raw))
		p->raw = atoi(f[1]);
	if (strlen(p->protos) < 25) {
		char s[20];

		if (!strcmp(f[2], "raw") || !strcmp(f[2], "upnp"))
			sprintf(s, "%s %s", !strcmp(f[2], "raw") ? "raw" : "UPnP", f[1]);
		else
			strcpy(s, !strcmp(f[2], "ipp") ? "IPP" : "LPD");
		if (!strstr(p->protos, s)) {
			if (p->protos[0])
				strcat(p->protos, ", ");
			strcat(p->protos, s);
		}
	}
}

static void fill_found(void)
{
	int i;
	char s[400], lang[80];
	XmString x;

	XmListDeleteAllItems(l_found);
	for (i = 0; i < nfound; i++) {
		languages(fd_[i].pdl, lang);
		sprintf(s, "%s   %s   [%s]   %s", fd_[i].model[0] ? fd_[i].model : fd_[i].name,
			fd_[i].ip, fd_[i].protos, lang);
		x = XmStringCreateLocalized(s);
		XmListAddItem(l_found, x, 0);
		XmStringFree(x);
	}
	sel_found = -1;
	XtSetSensitive(b_add, False);
	set_label(l_detail, " ");
}

static void find_done(void)
{
	char s[100];
	int st;

	XtRemoveInput(find_in);
	close(find_fd);
	find_fd = -1;
	waitpid(find_pid, &st, 0);
	find_buf[find_len] = 0;
	{
		char *l = find_buf, *e;

		for (; *l; l = e) {
			e = strchr(l, '\n');
			if (!e)
				break;
			*e++ = 0;
			add_line(l);
		}
	}
	fill_found();
	if (nfound)
		sprintf(s, "%d printer%s found on the network.", nfound, nfound == 1 ? "" : "s");
	else
		strcpy(s, "No printer answered (UPnP and Bonjour).  Is it switched on?");
	status(s);
	XtSetSensitive(b_search, True);
}

static void find_input(XtPointer cd, int *fd, XtInputId *id)
{
	int n = read(*fd, find_buf + find_len, sizeof find_buf - 1 - find_len);

	if (n > 0) {
		find_len += n;
		if (find_len < (int)sizeof find_buf - 1)
			return;
	}
	find_done();
}

static void search_cb(Widget w, XtPointer cd, XtPointer cb)
{
	int p[2];

	if (find_fd >= 0 || pipe(p) < 0)
		return;
	if ((find_pid = fork()) < 0) {
		close(p[0]);
		close(p[1]);
		return;
	}
	if (find_pid == 0) {
		close(p[0]);
		dup2(p[1], 1);
		close(p[1]);
		execl(FIND, "amixprfind", "-t", "3", (char *)NULL);
		_exit(127);
	}
	close(p[1]);
	find_fd = p[0];
	find_len = 0;
	nfound = 0;
	find_in = XtAppAddInput(app, find_fd, (XtPointer)XtInputReadMask, find_input, NULL);
	XtSetSensitive(b_search, False);
	XtSetSensitive(b_add, False);
	XmListDeleteAllItems(l_found);
	status("Searching the network (UPnP and Bonjour) ...");
}

/* ------------------------------------------------------------ actions */

/* a name for lp from the model: "Xerox WorkCentre 6515" -> "xerox6515" */
static void suggest(const struct found *p, char *out)
{
	const char *m = p->model[0] ? p->model : p->name;
	char brand[20] = "", num[20] = "";
	int i = 0, k = 0;

	while (m[i] && isalpha((unsigned char)m[i]) && k < 10)
		brand[k++] = tolower((unsigned char)m[i++]);
	brand[k] = 0;
	for (k = 0; m[i]; i++)
		if (isdigit((unsigned char)m[i]) && k < 4)
			num[k++] = m[i];
		else if (k)
			break;
	num[k] = 0;
	sprintf(out, "%s%s", brand[0] ? brand : "printer", num);
	for (i = 0; i < nlocal; i++)
		if (!strcmp(lc[i].name, out)) {
			strcat(out, "-2");
			break;
		}
}

static void found_cb(Widget w, XtPointer cd, XtPointer cb)
{
	XmListCallbackStruct *c = cb;
	struct found *p;
	char s[600], lang[80], name[30];

	sel_found = c->item_position - 1;
	if (sel_found < 0 || sel_found >= nfound)
		return;
	p = &fd_[sel_found];
	languages(p->pdl, lang);
	sprintf(s, "%s\nAddress %s, %s.  Languages: %s.%s%s", p->name, p->ip, p->protos, lang,
		p->admin[0] ? "\nSettings page: " : "", p->admin);
	set_label(l_detail, s);
	suggest(p, name);
	XmTextFieldSetString(t_name, name);
	XtSetSensitive(b_add, p->raw > 0);
	if (!p->raw)
		status("This printer offers no raw port; only raw TCP printing is supported yet.");
	else if (p->pdl[0] && !has(p->pdl, "application/postscript"))
		status("Note: this printer does not name PostScript; CDE prints PostScript.");
	else
		status(" ");
}

static int helper(const char *what, char *const argv[])
{
	char out[2000], s[2200];
	int r = run_out(argv, out, sizeof out);

	if (r != 0) {
		sprintf(s, "%s failed:\n%s", what, out[0] ? out : "(no message)");
		message(s, 1);
	}
	return r;
}

static void add_cb(Widget w, XtPointer cd, XtPointer cb)
{
	struct found *p;
	char port[8], desc[S + 30], *name, s[200];
	char *argv[] = { PRSET, "add", NULL, NULL, port, desc, NULL };
	int i, first;

	if (sel_found < 0 || sel_found >= nfound)
		return;
	p = &fd_[sel_found];
	name = XmTextFieldGetString(t_name);
	for (i = 0; name[i]; i++)
		name[i] = tolower((unsigned char)name[i]);
	sprintf(port, "%d", p->raw);
	sprintf(desc, "%.80s (%s:%d)", p->model[0] ? p->model : p->name, p->ip, p->raw);
	argv[2] = name;
	argv[3] = p->ip;
	first = nlocal == 0;
	if (helper("Adding the printer", argv) == 0) {
		if (first) {		/* the only one: the default at once */
			char *d[] = { PRSET, "default", name, NULL };

			helper("Making it the default", d);
		}
		sprintf(s, "Added %s%s.", name, first ? " as the default printer" : "");
		load_local();
		fill_local();
		status(s);
	}
	XtFree(name);
}

static void local_cb(Widget w, XtPointer cd, XtPointer cb)
{
	XmListCallbackStruct *c = cb;

	sel_local = c->item_position - 1;
	if (sel_local >= nlocal)
		sel_local = -1;
	XtSetSensitive(b_default, sel_local >= 0 && !lc[sel_local].dflt);
	XtSetSensitive(b_test, sel_local >= 0);
	XtSetSensitive(b_props, sel_local >= 0);
	XtSetSensitive(b_remove, sel_local >= 0);
}

static void local_action(Widget w, XtPointer cd, XtPointer cb)
{
	const char *what = cd;
	char name[20], s[200];
	char *argv[] = { PRSET, (char *)what, name, NULL };

	if (sel_local < 0)
		return;
	strcpy(name, lc[sel_local].name);
	if (helper(!strcmp(what, "test") ? "The test page" : !strcmp(what, "remove") ? "Removing the printer"
		   : "Making it the default", argv) != 0)
		return;
	if (!strcmp(what, "test"))
		sprintf(s, "Test page sent to %s.", name);
	else if (!strcmp(what, "remove"))
		sprintf(s, "Removed %s.", name);
	else
		sprintf(s, "%s is the default printer now.", name);
	load_local();
	fill_local();
	status(s);
}

/* ------------------------------------------------------------ properties */

static const char *const v_paper[] = { "a4", "a3", "a5", "letter", "legal", NULL };
static const char *const n_paper[] = { "A4", "A3", "A5", "Letter", "Legal", NULL };
static const char *const v_sides[] = { "one", "two-long", "two-short", NULL };
static const char *const n_sides[] = { "One-sided", "Two-sided (long edge, book)",
				       "Two-sided (short edge, flip)", NULL };
static const char *const v_color[] = { "color", "mono", NULL };
static const char *const n_color[] = { "Colour", "Black and white", NULL };

static Widget option_menu(Widget parent, const char *label_, const char *const *names)
{
	Widget menu = XmCreatePulldownMenu(parent, "menu", NULL, 0), opt, b;
	Arg a[2];
	int i;

	for (i = 0; names[i]; i++) {
		b = XtVaCreateManagedWidget("v", xmPushButtonWidgetClass, menu, XmNuserData, (XtPointer)(long)i, NULL);
		set_label(b, names[i]);
	}
	XtSetArg(a[0], XmNsubMenuId, menu);
	opt = XmCreateOptionMenu(parent, "opt", a, 1);
	set_label(XmOptionLabelGadget(opt), label_);
	XtManageChild(opt);
	return opt;
}

static void option_set(Widget opt, const char *const *values, const char *v)
{
	Widget menu;
	WidgetList kids;
	Cardinal nk;
	int i;

	XtVaGetValues(opt, XmNsubMenuId, &menu, NULL);
	XtVaGetValues(menu, XmNchildren, &kids, XmNnumChildren, &nk, NULL);
	for (i = 0; values[i] && i < (int)nk; i++)
		if (!strcmp(values[i], v))
			XtVaSetValues(opt, XmNmenuHistory, kids[i], NULL);
}

static const char *option_get(Widget opt, const char *const *values)
{
	Widget b;
	XtPointer ud;

	XtVaGetValues(opt, XmNmenuHistory, &b, NULL);
	XtVaGetValues(b, XmNuserData, &ud, NULL);
	return values[(long)ud];
}

/* the saved value of key in "paper=a4,sides=one,..." or def */
static void saved(const char *line, const char *key, char *out, const char *def)
{
	const char *p = line;
	int kl = strlen(key);

	strcpy(out, def);
	while (p && *p) {
		if (!strncmp(p, key, kl) && p[kl] == '=') {
			int n = strcspn(p + kl + 1, ",\n");

			if (n < 20) {
				memcpy(out, p + kl + 1, n);
				out[n] = 0;
			}
		}
		p = strchr(p, ',');
		if (p)
			p++;
	}
}

static char prop_name[20];

static void props_cb(Widget w, XtPointer cd, XtPointer cb)
{
	char path[100], line[200] = "", v[24], s[100];
	FILE *f;

	if (sel_local < 0)
		return;
	strcpy(prop_name, lc[sel_local].name);
	sprintf(path, "%s/%s", OPTDIR, prop_name);
	if ((f = fopen(path, "r"))) {
		if (!fgets(line, sizeof line, f))
			line[0] = 0;
		fclose(f);
	}
	saved(line, "paper", v, "a4");
	option_set(o_paper, v_paper, v);
	saved(line, "sides", v, "one");
	option_set(o_sides, v_sides, v);
	saved(line, "color", v, "color");
	option_set(o_color, v_color, v);
	sprintf(s, "Defaults for %s (a job's lp -o options win over them):", prop_name);
	set_label(l_propname, s);
	XtManageChild(propdlg);
}

static void props_save(Widget w, XtPointer cd, XtPointer cb)
{
	char opts[100], s[150];
	char *argv[] = { PRSET, "set", prop_name, opts, NULL };

	sprintf(opts, "paper=%s,sides=%s,color=%s", option_get(o_paper, v_paper),
		option_get(o_sides, v_sides), option_get(o_color, v_color));
	XtUnmanageChild(propdlg);
	if (helper("Saving the properties", argv) == 0) {
		sprintf(s, "%s: %s.", prop_name, opts);
		status(s);
	}
}

static void props_cancel(Widget w, XtPointer cd, XtPointer cb)
{
	XtUnmanageChild(propdlg);
}

static void make_propdlg(void)
{
	Widget f, rc, bar, b;

	propdlg = XmCreateFormDialog(top, "properties", NULL, 0);
	XtVaSetValues(XtParent(propdlg), XmNtitle, "Printer Properties", NULL);
	XtVaSetValues(propdlg, XmNmarginWidth, 10, XmNmarginHeight, 10, NULL);
	f = propdlg;
	l_propname = XtVaCreateManagedWidget("name", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
					     XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
					     XmNrightAttachment, XmATTACH_FORM, NULL);
	rc = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, f, XmNorientation, XmVERTICAL,
				     XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l_propname, XmNtopOffset, 8,
				     XmNleftAttachment, XmATTACH_FORM, NULL);
	o_paper = option_menu(rc, "Paper:  ", n_paper);
	o_sides = option_menu(rc, "Sides:  ", n_sides);
	o_color = option_menu(rc, "Colour: ", n_color);
	bar = XtVaCreateManagedWidget("bar", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, rc, XmNtopOffset, 12,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	b = XtVaCreateManagedWidget("save", xmPushButtonWidgetClass, bar, NULL);
	set_label(b, "Save");
	XtAddCallback(b, XmNactivateCallback, props_save, NULL);
	b = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, bar, NULL);
	set_label(b, "Cancel");
	XtAddCallback(b, XmNactivateCallback, props_cancel, NULL);
}

static void close_cb(Widget w, XtPointer cd, XtPointer cb)
{
	exit(0);
}

/* ------------------------------------------------------------ window */

static Widget label(Widget parent, Widget above, const char *text)
{
	Widget l = XtVaCreateManagedWidget("label", xmLabelWidgetClass, parent,
					   XmNalignment, XmALIGNMENT_BEGINNING,
					   XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM,
					   XmNtopWidget, above, XmNtopOffset, 6,
					   XmNleftAttachment, XmATTACH_FORM,
					   XmNrightAttachment, XmATTACH_FORM, NULL);
	set_label(l, text);
	return l;
}

static Widget framed_list(Widget form, Widget above, const char *title, Widget *list, int rows)
{
	Widget fr, t;
	Arg a[6];
	int n = 0;

	fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, above, XmNtopOffset, 10,
				     XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
	t = XtVaCreateManagedWidget("title", xmLabelWidgetClass, fr, XmNchildType, XmFRAME_TITLE_CHILD, NULL);
	set_label(t, title);
	XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
	XtSetArg(a[n], XmNvisibleItemCount, rows); n++;
	XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
	XtSetArg(a[n], XmNwidth, 620); n++;
	*list = XmCreateScrolledList(fr, "list", a, n);
	XtManageChild(*list);
	return fr;
}

static Widget button(Widget bar, const char *text, XtCallbackProc cb, XtPointer cd)
{
	Widget b = XtVaCreateManagedWidget("b", xmPushButtonWidgetClass, bar, NULL);

	set_label(b, text);
	XtAddCallback(b, XmNactivateCallback, cb, cd);
	return b;
}

int main(int argc, char **argv)
{
	Widget form, intro, fr1, row, nl, fr2, bar2, bar;

	signal(SIGPIPE, SIG_IGN);
	top = XtVaOpenApplication(&app, "Amixprinters", NULL, 0, &argc, argv, NULL, sessionShellWidgetClass,
				  XmNtitle, "Printers", NULL);
	if (argc > 1) {
		fprintf(stderr, "usage: amixprinters\n");
		return 2;
	}
	form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top, XmNmarginWidth, 10,
				       XmNmarginHeight, 10, NULL);
	intro = label(form, NULL, "Printers on the network answer UPnP or Bonjour.  Choose one and "
				  "add it; it then prints for every CDE application.");
	fr1 = framed_list(form, intro, "Printers on the network", &l_found, 5);
	XtAddCallback(l_found, XmNbrowseSelectionCallback, found_cb, NULL);
	l_detail = label(form, fr1, " ");

	row = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l_detail, XmNtopOffset, 6,
				      XmNleftAttachment, XmATTACH_FORM, NULL);
	nl = XtVaCreateManagedWidget("nl", xmLabelWidgetClass, row, NULL);
	set_label(nl, "Name for it:");
	t_name = XtVaCreateManagedWidget("name", xmTextFieldWidgetClass, row, XmNcolumns, 14,
					 XmNmaxLength, 14, NULL);
	b_add = button(row, "Add Printer", add_cb, NULL);
	b_search = button(row, "Search Again", search_cb, NULL);

	fr2 = framed_list(form, row, "Printers on this system (lp)", &l_local, 4);
	XtAddCallback(l_local, XmNbrowseSelectionCallback, local_cb, NULL);
	bar2 = XtVaCreateManagedWidget("bar2", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
				       XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, fr2, XmNtopOffset, 6,
				       XmNleftAttachment, XmATTACH_FORM, NULL);
	b_default = button(bar2, "Make Default", local_action, "default");
	b_test = button(bar2, "Print Test Page", local_action, "test");
	b_props = button(bar2, "Properties...", props_cb, NULL);
	b_remove = button(bar2, "Remove", local_action, "remove");

	l_status = label(form, bar2, " ");
	bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
				      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l_status, XmNtopOffset, 10,
				      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
	button(bar, "Close", close_cb, NULL);

	XtSetSensitive(b_add, False);
	make_propdlg();
	load_local();
	fill_local();
	XtRealizeWidget(top);
	search_cb(NULL, NULL, NULL);
	XtAppMainLoop(app);
	return 0;
}
