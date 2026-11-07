/*
 * amixdisplay.c -- the Display settings for AMIX in CDE, after the Linux
 * desktops' panels: the screen resolution, refresh and colours of the
 * Xzz9000 server on the Z3660.
 *
 * The change is live through RandR 1.1 (x11/xzz9000 local-randr.patch).
 * After it, a dialog asks "Keep this screen mode?" for 20 seconds and goes
 * back to the old one unless Keep is pressed, so a mode the monitor cannot
 * show undoes itself.  dtwm does not follow a root window resize, so it is
 * restarted (f.restart -noconfirm through _DT_WM_REQUEST) after each change
 * of size; the dialog comes 3 s later, when dtwm has its windows again (a
 * dialog mapped during the restart stayed hidden).  A new rate at the same
 * size keeps the geometry: no restart, the dialog at once.
 *
 * Defaults:
 *   $HOME/.amixdisplay        the user's own, applied at login
 *                             (Xsession.d: amixdisplay -restore)
 *   /etc/dt/config/rtg-mode   the login screen's, for everyone; written by
 *                             the setuid helper /opt/amix/lib/amixdisplay/setlogin,
 *                             read by Xzz9000-dt at server start and applied
 *                             again before each greeter (Xsetup:
 *                             amixdisplay -system)
 * Both files: line 1 "WxH" and nothing else (Xzz9000-dt passes the whole
 * line to -mode), line 2 optional "rate N" (Hz).
 *
 *   amixdisplay               the window
 *   amixdisplay -restore      apply $HOME/.amixdisplay, no window
 *   amixdisplay -system       apply /etc/dt/config/rtg-mode, no window
 *
 * Without RandR (an older Xzz9000) the window still saves the login
 * screen's mode, which applies when the X server starts again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/RowColumn.h>
#include <Xm/MessageB.h>
#include <Xm/Protocols.h>
#include <Xm/AtomMgr.h>

#define SYSFILE     "/etc/dt/config/rtg-mode"
#define USERFILE    ".amixdisplay"
#define SETLOGIN    "/opt/amix/lib/amixdisplay/setlogin"
#define KEEP_SECS   20
#define KEEP_WAIT_MS 3000   /* after a size change: dtwm restarts first */
#define MAXSIZES    64

/* without RandR: the modes the Z3660 kernel driver and every Xzz9000
   build know (zz9000hw.c before RandR, minus the ZZ9000-only 1920x800) */
static const short fallback[][2] = {
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 },
    { 1280, 800 }, { 1280, 1024 }, { 1920, 1080 }
};

struct mode {
    int w, h;
    short rates[16];
    int nrates;
};

static Display *dpy;
static Window root;
static int scr;
static int have_rr;
static struct mode modes[MAXSIZES];
static int nmodes;
static int order[MAXSIZES];              /* list position -> mode, smallest first */
static int nlisted;
static int cur_size = -1, cur_rate;     /* what the server shows now */
static int sel_size = -1, sel_rate;     /* what the window has selected */

static XtAppContext app;
static Widget top, list, l_now, l_saved, l_info, rate_menu, rate_opt, depth_opt, t_user, t_login, keepdlg;
static int keep_left, keep_resized;
static XtIntervalId keep_timer;
static int old_size, old_rate;

/* ------------------------------------------------------------ the modes */

static int gcd(int a, int b)
{
    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static const char *aspect(int w, int h)
{
    static char s[16];
    int g;

    if (w * 9 == h * 16 || (w == 1366 && h == 768))
        return "16:9";
    if (w * 10 == h * 16)
        return "16:10";
    g = gcd(w, h);
    sprintf(s, "%d:%d", w / g, h / g);
    return s;
}

/* read the server's sizes and rates (RandR) or the fallback list */
static void load_modes(void)
{
    int i, j, n;

    nmodes = 0;
    cur_size = -1;
    cur_rate = 0;
    if (have_rr) {
        XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);
        XRRScreenSize *sz;
        Rotation rot;

        if (sc) {
            sz = XRRConfigSizes(sc, &n);
            for (i = 0; i < n && i < MAXSIZES; i++) {
                short *r;
                int nr;

                modes[i].w = sz[i].width;
                modes[i].h = sz[i].height;
                r = XRRConfigRates(sc, i, &nr);
                modes[i].nrates = 0;
                for (j = 0; j < nr && j < 16; j++)
                    modes[i].rates[modes[i].nrates++] = r[j];
            }
            nmodes = i;
            cur_size = XRRConfigCurrentConfiguration(sc, &rot);
            cur_rate = XRRConfigCurrentRate(sc);
            XRRFreeScreenConfigInfo(sc);
            return;
        }
        have_rr = 0;
    }
    for (i = 0; i < (int)(sizeof fallback / sizeof fallback[0]); i++) {
        modes[i].w = fallback[i][0];
        modes[i].h = fallback[i][1];
        modes[i].nrates = 0;
        if (modes[i].w == DisplayWidth(dpy, scr) && modes[i].h == DisplayHeight(dpy, scr))
            cur_size = i;
    }
    nmodes = i;
}

static int find_mode(int w, int h)
{
    int i;

    for (i = 0; i < nmodes; i++)
        if (modes[i].w == w && modes[i].h == h)
            return i;
    return -1;
}

/* the file format: line 1 "WxH", line 2 "rate N" */
static int read_conf(const char *path, int *w, int *h, int *rate)
{
    FILE *f = fopen(path, "r");
    char line[80];

    *rate = 0;
    if (!f)
        return 0;
    if (!fgets(line, sizeof line, f) || sscanf(line, "%dx%d", w, h) != 2) {
        fclose(f);
        return 0;
    }
    if (fgets(line, sizeof line, f))
        sscanf(line, "rate %d", rate);
    fclose(f);
    return *w > 0 && *h > 0;
}

static char *user_path(void)
{
    static char p[1024];
    const char *home = getenv("HOME");

    sprintf(p, "%.1000s/%s", home && *home ? home : "/", USERFILE);
    return p;
}

static int write_user(int w, int h, int rate, char *err)
{
    char tmp[1100], *p = user_path();
    FILE *f;

    sprintf(tmp, "%s.new", p);
    f = fopen(tmp, "w");
    if (!f) {
        sprintf(err, "Cannot write %.900s: %s", tmp, strerror(errno));
        return -1;
    }
    fprintf(f, "%dx%d\n", w, h);
    if (rate)
        fprintf(f, "rate %d\n", rate);
    fprintf(f, "# amixdisplay: this user's screen mode, applied at login\n");
    if (fclose(f) != 0 || rename(tmp, p) != 0) {
        sprintf(err, "Cannot write %.900s: %s", p, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* the login screen's mode, through the setuid helper */
static int write_system(int w, int h, int rate, char *err)
{
    char res[32], rt[16];
    pid_t pid;
    int st;

    sprintf(res, "%dx%d", w, h);
    sprintf(rt, "%d", rate);
    pid = fork();
    if (pid < 0) {
        sprintf(err, "fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execl(SETLOGIN, "setlogin", res, rt, (char *)0);
        _exit(127);
    }
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        sprintf(err, WIFEXITED(st) && WEXITSTATUS(st) == 127 ?
                "%s is missing" : "%s refused the mode", SETLOGIN);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------- changing the screen */

/* dtwm keeps the old screen size; have it restart, without its question */
static void restart_wm(void)
{
    Atom info = XInternAtom(dpy, "_MOTIF_WM_INFO", True), type;
    Atom req = XInternAtom(dpy, "_DT_WM_REQUEST", False);
    int fmt;
    unsigned long n, left;
    unsigned char *data = NULL;
    static const char cmd[] = "f.restart -noconfirm";

    if (info == None)
        return;
    if (XGetWindowProperty(dpy, root, info, 0, 2, False, info, &type, &fmt, &n, &left,
                           &data) == Success && data && fmt == 32 && n >= 2) {
        Window wm = (Window)((long *)data)[1];

        XChangeProperty(dpy, wm, req, XA_STRING, 8, PropModeAppend,
                        (unsigned char *)cmd, sizeof cmd);
        XFlush(dpy);
    }
    if (data)
        XFree(data);
}

/* switch to size i at rate (0: the size's first); 0 on success */
static int set_mode(int i, int rate)
{
    XRRScreenConfiguration *sc;
    Status s;
    Rotation rot;

    if (!have_rr || i < 0 || i >= nmodes)
        return -1;
    sc = XRRGetScreenInfo(dpy, root);
    if (!sc)
        return -1;
    XRRConfigCurrentConfiguration(sc, &rot);
    if (rate && modes[i].nrates > 1)
        s = XRRSetScreenConfigAndRate(dpy, sc, root, i, RR_Rotate_0, (short)rate, CurrentTime);
    else
        s = XRRSetScreenConfig(dpy, sc, root, i, RR_Rotate_0, CurrentTime);
    XRRFreeScreenConfigInfo(sc);
    XSync(dpy, False);
    return s == Success ? 0 : -1;
}

/* -restore / -system: apply a file's mode if the server has it */
static int apply_file(const char *path)
{
    int w, h, rate, i;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "amixdisplay: cannot open the display\n");
        return 1;
    }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    {
        int eb, erb;

        have_rr = XRRQueryExtension(dpy, &eb, &erb);
    }
    if (!have_rr || !read_conf(path, &w, &h, &rate)) {
        XCloseDisplay(dpy);
        return 0;
    }
    load_modes();
    i = find_mode(w, h);
    if (i < 0) {
        fprintf(stderr, "amixdisplay: %s: %dx%d is not offered by this server\n", path, w, h);
        XCloseDisplay(dpy);
        return 1;
    }
    if (i != cur_size || (rate && rate != cur_rate)) {
        if (set_mode(i, rate) != 0) {
            fprintf(stderr, "amixdisplay: %dx%d refused\n", w, h);
            XCloseDisplay(dpy);
            return 1;
        }
        restart_wm();       /* nothing happens when no dtwm runs yet */
    }
    XCloseDisplay(dpy);
    return 0;
}

/* --------------------------------------------------------------- window */

static void set_label(Widget w, const char *s)
{
    XmString x = XmStringCreateLocalized((char *)s);

    XtVaSetValues(w, XmNlabelString, x, NULL);
    XmStringFree(x);
}

static void mode_text(char *s, int w, int h, int rate)
{
    if (rate)
        sprintf(s, "%d x %d, %d Hz", w, h, rate);
    else
        sprintf(s, "%d x %d", w, h);
}

static void show_state(void)
{
    char s[300], a[80], b[80];
    int w, h, rate;

    if (cur_size >= 0)
        mode_text(a, modes[cur_size].w, modes[cur_size].h, cur_rate);
    else
        mode_text(a, DisplayWidth(dpy, scr), DisplayHeight(dpy, scr), 0);
    sprintf(s, "Now: %s, %d bit%s", a, DefaultDepth(dpy, scr),
            have_rr ? "" : "   (this X server cannot change mode while it runs)");
    set_label(l_now, s);

    strcpy(a, "none");
    if (read_conf(user_path(), &w, &h, &rate))
        mode_text(a, w, h, rate);
    strcpy(b, "1920 x 1080 (built in)");
    if (read_conf(SYSFILE, &w, &h, &rate))
        mode_text(b, w, h, rate);
    sprintf(s, "Your default: %s      Login screen: %s", a, b);
    set_label(l_saved, s);
}

static void show_info(void)
{
    char s[200];
    double mb;

    if (sel_size < 0)
        return;
    mb = (double)modes[sel_size].w * modes[sel_size].h * 2 / (1024.0 * 1024.0);
    sprintf(s, "%s, frame %.1f MB", aspect(modes[sel_size].w, modes[sel_size].h), mb);
    set_label(l_info, s);
}

static void rate_cb(Widget w, XtPointer cd, XtPointer cb)
{
    sel_rate = (int)(long)cd;
}

/* the refresh menu for the selected size */
static void fill_rates(void)
{
    WidgetList kids;
    Cardinal nk;
    Widget b, first = NULL;
    char s[32];
    int i;

    XtVaGetValues(rate_menu, XmNchildren, &kids, XmNnumChildren, &nk, NULL);
    while (nk > 0) {
        XtDestroyWidget(kids[--nk]);
    }
    sel_rate = 0;
    if (sel_size < 0 || modes[sel_size].nrates == 0) {
        b = XtVaCreateManagedWidget("r", xmPushButtonWidgetClass, rate_menu, NULL);
        set_label(b, have_rr ? "as the board sets it" : "60 Hz (the board's)");
        XtVaSetValues(rate_opt, XmNmenuHistory, b, NULL);
        XtSetSensitive(rate_opt, False);
        return;
    }
    for (i = 0; i < modes[sel_size].nrates; i++) {
        sprintf(s, "%d Hz", modes[sel_size].rates[i]);
        b = XtVaCreateManagedWidget("r", xmPushButtonWidgetClass, rate_menu, NULL);
        set_label(b, s);
        XtAddCallback(b, XmNactivateCallback, rate_cb, (XtPointer)(long)modes[sel_size].rates[i]);
        if (!first || (sel_size == cur_size && modes[sel_size].rates[i] == cur_rate)) {
            first = b;
            sel_rate = modes[sel_size].rates[i];
        }
    }
    XtVaSetValues(rate_opt, XmNmenuHistory, first, NULL);
    XtSetSensitive(rate_opt, modes[sel_size].nrates > 1);
}

static void list_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *l = (XmListCallbackStruct *)cb;

    sel_size = order[l->item_position - 1];
    fill_rates();
    show_info();
}

static void fill_list(void)
{
    char s[80];
    int i;
    XmString x;

    int j, k, pos = 0;

    /* RandR's size index is the server's order; show them by pixel count */
    for (i = 0; i < nmodes; i++)
        order[i] = i;
    for (i = 1; i < nmodes; i++)
        for (j = i; j > 0; j--) {
            long a = (long)modes[order[j - 1]].w * modes[order[j - 1]].h;
            long b = (long)modes[order[j]].w * modes[order[j]].h;

            if (a < b || (a == b && modes[order[j - 1]].w <= modes[order[j]].w))
                break;
            k = order[j];
            order[j] = order[j - 1];
            order[j - 1] = k;
        }
    /* 640x512 is left out (the user, 2026-11-07): the firmware shows it in
       the 800x600 timing, with borders */
    for (i = j = 0; i < nmodes; i++)
        if (!(modes[order[i]].w == 640 && modes[order[i]].h == 512) || order[i] == cur_size)
            order[j++] = order[i];
    nlisted = j;
    XmListDeleteAllItems(list);
    for (i = 0; i < nlisted; i++) {
        k = order[i];
        sprintf(s, "%d x %d   (%s)%s", modes[k].w, modes[k].h, aspect(modes[k].w, modes[k].h),
                k == cur_size ? "   - now" : "");
        x = XmStringCreateLocalized(s);
        XmListAddItemUnselected(list, x, 0);
        XmStringFree(x);
        if (k == cur_size)
            pos = i;
    }
    sel_size = order[pos];
    XmListSelectPos(list, pos + 1, False);
    XmListSetPos(list, pos + 1 > 8 ? pos - 6 : 1);
    fill_rates();
    show_info();
}

static void destroy_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XtDestroyWidget(w);
}

static void message(const char *title, const char *text, int error)
{
    Widget d = error ? XmCreateErrorDialog(top, "error", NULL, 0)
                     : XmCreateInformationDialog(top, "info", NULL, 0);
    XmString x = XmStringCreateLtoR((char *)text, XmFONTLIST_DEFAULT_TAG);

    XtVaSetValues(d, XmNmessageString, x, NULL);
    XtVaSetValues(XtParent(d), XmNtitle, title, NULL);
    XmStringFree(x);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtAddCallback(d, XmNokCallback, destroy_cb, NULL);
    XtManageChild(d);
}

/* save the selection as the toggles say; returns 0 or shows the error */
static int save(int i, int rate)
{
    char err[1200];
    int w = modes[i].w, h = modes[i].h;

    if (XmToggleButtonGetState(t_user) && write_user(w, h, rate, err) != 0) {
        message("Display", err, 1);
        return -1;
    }
    if (XmToggleButtonGetState(t_login) && write_system(w, h, rate, err) != 0) {
        message("Display", err, 1);
        return -1;
    }
    return 0;
}

/* keep a shell on the screen after it got smaller */
static void fit_on_screen(Widget shell, int sw, int sh)
{
    Position x, y;
    Dimension w, h;

    XtVaGetValues(shell, XmNx, &x, XmNy, &y, XmNwidth, &w, XmNheight, &h, NULL);
    if (x + (int)w > sw)
        x = sw > (int)w ? sw - w : 0;
    if (y + (int)h > sh)
        y = sh > (int)h ? sh - h : 0;
    XtVaSetValues(shell, XmNx, x, XmNy, y, NULL);
}

static void after_change(void)
{
    load_modes();
    fill_list();
    show_state();
    if (cur_size >= 0)
        fit_on_screen(top, modes[cur_size].w, modes[cur_size].h);
}

static void keep_done(int keep)
{
    if (keep_timer) {
        XtRemoveTimeOut(keep_timer);
        keep_timer = 0;
    }
    XtUnmanageChild(keepdlg);
    if (keep) {
        if (save(cur_size, cur_rate) == 0)
            show_state();
        return;
    }
    if (set_mode(old_size, old_rate) != 0)
        message("Display", "Could not go back to the old screen mode.", 1);
    if (keep_resized)
        restart_wm();
    after_change();
}

static void keep_tick(XtPointer cd, XtIntervalId *id)
{
    char s[200];
    XmString x;

    keep_timer = 0;
    if (--keep_left <= 0) {
        keep_done(0);
        return;
    }
    sprintf(s, "Keep this screen mode?\n\nGoing back to the old one in %d s.", keep_left);
    x = XmStringCreateLtoR(s, XmFONTLIST_DEFAULT_TAG);
    XtVaSetValues(keepdlg, XmNmessageString, x, NULL);
    XmStringFree(x);
    keep_timer = XtAppAddTimeOut(app, 1000, keep_tick, NULL);
}

static void keep_cb(Widget w, XtPointer cd, XtPointer cb)
{
    keep_done((int)(long)cd);
}

static void ask_keep(void)
{
    Dimension w, h;
    int sw = modes[cur_size].w, sh = modes[cur_size].h;

    keep_left = KEEP_SECS + 1;
    keep_tick(NULL, NULL);
    XtManageChild(keepdlg);
    XtVaGetValues(XtParent(keepdlg), XmNwidth, &w, XmNheight, &h, NULL);
    XtVaSetValues(XtParent(keepdlg), XmNx, (sw - (int)w) / 2, XmNy, (sh - (int)h) / 2, NULL);
    if (XtIsRealized(XtParent(keepdlg)))
        XRaiseWindow(dpy, XtWindow(XtParent(keepdlg)));
}

static void ask_keep_later(XtPointer cd, XtIntervalId *id)
{
    ask_keep();
}

static void apply_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char s[400];
    int i = sel_size;

    if (i < 0)
        return;
    if (!have_rr || (i == cur_size && (!sel_rate || sel_rate == cur_rate))) {
        /* nothing to switch: save only.  Every mode saved is one the server
           is showing (or, without RandR, one of the fallback list that every
           Xzz9000 knows): a mode unknown to the server at its start would
           keep dtlogin from starting X at all */
        if (save(i, sel_rate) == 0) {
            show_state();
            if (!have_rr && i != cur_size) {
                sprintf(s, "Saved.  This X server cannot change its mode while it runs:\n"
                           "%d x %d applies when X starts again (log out, or restart dtlogin).",
                        modes[i].w, modes[i].h);
                message("Display", s, 0);
            }
        }
        return;
    }
    old_size = cur_size;
    old_rate = cur_rate;
    if (set_mode(i, sel_rate) != 0) {
        sprintf(s, "The X server refused %d x %d.", modes[i].w, modes[i].h);
        message("Display", s, 1);
        after_change();
        return;
    }
    keep_resized = i != old_size;
    if (keep_resized)
        restart_wm();
    after_change();
    if (keep_resized)
        XtAppAddTimeOut(app, KEEP_WAIT_MS, ask_keep_later, NULL);
    else
        ask_keep();
}

static void close_cb(Widget w, XtPointer cd, XtPointer cb)
{
    exit(0);
}

static Widget hsep_label(Widget parent, Widget above, const char *text)
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

int main(int argc, char **argv)
{
    Widget form, l_mon, fr, rc, menu, b, bar, apply, close;
    Arg a[12];
    int n, eb, erb, i, *depths, nd;
    char s[80];
    Atom del;

    if (argc == 2 && strcmp(argv[1], "-restore") == 0)
        return apply_file(user_path());
    if (argc == 2 && strcmp(argv[1], "-system") == 0)
        return apply_file(SYSFILE);

    top = XtVaOpenApplication(&app, "Amixdisplay", NULL, 0, &argc, argv, NULL, sessionShellWidgetClass,
                              XmNtitle, "Display", NULL);
    if (argc > 1) {
        fprintf(stderr, "usage: amixdisplay [-restore | -system]\n");
        return 2;
    }
    dpy = XtDisplay(top);
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    have_rr = XRRQueryExtension(dpy, &eb, &erb);
    load_modes();

    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top, XmNmarginWidth, 10,
                                   XmNmarginHeight, 10, NULL);
    l_mon = hsep_label(form, NULL, "Monitor: Z3660 HDMI output (it reads nothing from the "
                                   "monitor: every board mode is listed)");
    l_now = hsep_label(form, l_mon, "");
    l_saved = hsep_label(form, l_now, "");

    fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET,
                                 XmNtopWidget, l_saved, XmNtopOffset, 10,
                                 XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    b = XtVaCreateManagedWidget("Resolution", xmLabelWidgetClass, fr,
                                XmNchildType, XmFRAME_TITLE_CHILD, NULL);
    n = 0;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(a[n], XmNvisibleItemCount, 10); n++;
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
    list = XmCreateScrolledList(fr, "sizes", a, n);
    XtManageChild(list);
    XtAddCallback(list, XmNbrowseSelectionCallback, list_cb, NULL);

    rc = XtVaCreateManagedWidget("choices", xmRowColumnWidgetClass, form, XmNorientation, XmVERTICAL,
                                 XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, fr, XmNtopOffset, 8,
                                 XmNleftAttachment, XmATTACH_FORM, NULL);
    rate_menu = XmCreatePulldownMenu(rc, "rates", NULL, 0);
    n = 0;
    XtSetArg(a[n], XmNsubMenuId, rate_menu); n++;
    rate_opt = XmCreateOptionMenu(rc, "rate", a, n);
    set_label(XmOptionLabelGadget(rate_opt), "Refresh: ");
    XtManageChild(rate_opt);

    menu = XmCreatePulldownMenu(rc, "depths", NULL, 0);
    depths = XListDepths(dpy, scr, &nd);
    for (i = 0; depths && i < nd; i++) {
        if (depths[i] == 1)
            continue;
        if (depths[i] == 16)
            strcpy(s, "16 bit (65 536 colours)");
        else
            sprintf(s, "%d bit", depths[i]);
        b = XtVaCreateManagedWidget("d", xmPushButtonWidgetClass, menu, NULL);
        set_label(b, s);
        if (depths[i] == DefaultDepth(dpy, scr))
            XtVaSetValues(menu, XmNmenuHistory, b, NULL);
    }
    if (depths)
        XFree(depths);
    n = 0;
    XtSetArg(a[n], XmNsubMenuId, menu); n++;
    depth_opt = XmCreateOptionMenu(rc, "depth", a, n);
    set_label(XmOptionLabelGadget(depth_opt), "Colours: ");
    XtManageChild(depth_opt);
    /* the Z3660's HDMI path is RGB565 (xzz9000 docs/DEPTH-24-32-EVALUATION.md):
       the server has one depth, and another one would need a server restart */
    XtSetSensitive(depth_opt, False);

    l_info = hsep_label(form, rc, "");
    t_user = XtVaCreateManagedWidget("user", xmToggleButtonWidgetClass, form, XmNset, True,
                                     XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l_info, XmNtopOffset, 8,
                                     XmNleftAttachment, XmATTACH_FORM, NULL);
    set_label(t_user, "Save as my default (applied when I log in)");
    t_login = XtVaCreateManagedWidget("login", xmToggleButtonWidgetClass, form, XmNset, False,
                                      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, t_user,
                                      XmNleftAttachment, XmATTACH_FORM, NULL);
    set_label(t_login, "Also use it for the login screen (all users)");

    bar = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
                                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, t_login, XmNtopOffset, 12,
                                  XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
    apply = XtVaCreateManagedWidget("apply", xmPushButtonWidgetClass, bar, NULL);
    set_label(apply, "Apply");
    XtAddCallback(apply, XmNactivateCallback, apply_cb, NULL);
    close = XtVaCreateManagedWidget("close", xmPushButtonWidgetClass, bar, NULL);
    set_label(close, "Close");
    XtAddCallback(close, XmNactivateCallback, close_cb, NULL);

    keepdlg = XmCreateQuestionDialog(top, "keep", NULL, 0);
    XtVaSetValues(XtParent(keepdlg), XmNtitle, "Display", NULL);
    XtVaSetValues(keepdlg, XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL,
                  XmNdefaultPosition, False, XmNnoResize, True, NULL);
    set_label(XmMessageBoxGetChild(keepdlg, XmDIALOG_OK_BUTTON), "Keep");
    set_label(XmMessageBoxGetChild(keepdlg, XmDIALOG_CANCEL_BUTTON), "Go Back");
    XtUnmanageChild(XmMessageBoxGetChild(keepdlg, XmDIALOG_HELP_BUTTON));
    XtAddCallback(keepdlg, XmNokCallback, keep_cb, (XtPointer)1);
    XtAddCallback(keepdlg, XmNcancelCallback, keep_cb, (XtPointer)0);
    del = XmInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(XtParent(keepdlg), del, keep_cb, (XtPointer)0);
    XtVaSetValues(XtParent(keepdlg), XmNdeleteResponse, XmDO_NOTHING, NULL);

    fill_list();
    show_state();
    XtRealizeWidget(top);
    XtAppMainLoop(app);
    return 0;
}
