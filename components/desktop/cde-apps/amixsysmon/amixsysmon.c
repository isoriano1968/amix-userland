/*
 * amixsysmon.c -- a system monitor for AMIX in CDE, after GNOME's: the
 * processes, the CPU, memory, swap and network over the last minute, the
 * file systems, and the Z3660 (ztop).  Its numbers come from libamixstat
 * (library/amixstat); it is installed setgid sys for /dev/kmem.
 *
 * CPU: AMIX books idle ticks as system time of the process that ran last, so
 * a process's system time is scaled to the kernel's real share, which the
 * profiler gives (struct ast_cpu); its user time is right as it is.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/RowColumn.h>
#include <Xm/Notebook.h>
#include <Xm/DrawingA.h>
#include <Xm/Text.h>
#include <Xm/MessageB.h>
#include <amixstat.h>

#define NHIST   60              /* samples in a graph: one a second */
#define ZTOP    "/usr/amiga/bin/ztop"

enum { TAB_PROCS = 1, TAB_RES, TAB_FS, TAB_Z3660 };
enum { SORT_CPU, SORT_MEM, SORT_PID, SORT_NAME };

static XtAppContext app;
static Widget top, nb, plist, pstatus, gcpu, gmem, gnet, lcpu, lmem, lnet, fsarea, ztext, status;
static Widget confirm;
static GC gc;
static Pixel c_bg, c_grid, c_user, c_kern, c_mem, c_swap, c_in, c_out, c_text, c_bar, c_barbg;
static XFontStruct *font;

/* the graphs' history, newest last; values in percent or packets a second */
static double h_user[NHIST], h_kern[NHIST], h_mem[NHIST], h_swap[NHIST], h_in[NHIST], h_out[NHIST];
static int nhist;

static struct ast_cpu cpu0;
static int have_cpu0;
static unsigned long net_in0, net_out0;
static int have_net0;
static char netname[16] = "zen0";
static struct ast_mem mem;

/* the processes: the last two samples, and the shown order */
struct row {
    struct ast_proc p;
    double pcpu;                /* percent over the last interval */
};
static struct ast_proc *prev;
static int nprev;
static double prev_t;           /* lbolt / hz of prev */
static struct row *rows;
static int nrows, sortby = SORT_CPU, only_mine;
static pid_t *shown;            /* pid of each list line */
static double kern_share = 0;   /* the kernel's real share of the last proc interval */

static struct ast_fs *fs;
static int nfs;

static int tick;

/* ---------------------------------------------------------------- helpers */

static Pixel colour(const char *name, Pixel fallback)
{
    XColor c, e;
    Colormap cm = DefaultColormapOfScreen(XtScreen(top));

    if (XAllocNamedColor(XtDisplay(top), cm, name, &c, &e))
        return c.pixel;
    return fallback;
}

static void set_label(Widget w, const char *s)
{
    XmString x = XmStringCreateLtoR((char *)s, XmFONTLIST_DEFAULT_TAG);

    XtVaSetValues(w, XmNlabelString, x, NULL);
    XmStringFree(x);
}

static int current_tab(void)
{
    int n = 0;

    XtVaGetValues(nb, XmNcurrentPageNumber, &n, NULL);
    return n;
}

static void push(double *h, double v)
{
    if (nhist < NHIST)
        h[nhist] = v;
    else {
        memmove(h, h + 1, (NHIST - 1) * sizeof *h);
        h[NHIST - 1] = v;
    }
}

static const char *user_name(uid_t uid)
{
    static uid_t cu[64];
    static char cn[64][9];
    static int n;
    struct passwd *pw;
    int i;

    for (i = 0; i < n; i++)
        if (cu[i] == uid)
            return cn[i];
    if (n == 64)
        n = 0;
    cu[n] = uid;
    if ((pw = getpwuid(uid)) != NULL)
        sprintf(cn[n], "%.8s", pw->pw_name);
    else
        sprintf(cn[n], "%ld", (long)uid);
    return cn[n++];
}

/* KB, as libamixstat gives sizes, for people */
static void human(char *buf, unsigned long kb)
{
    if (kb >= 1048576UL)
        sprintf(buf, "%.1f GB", kb / 1048576.0);
    else if (kb >= 1024UL)
        sprintf(buf, "%.1f MB", kb / 1024.0);
    else
        sprintf(buf, "%lu KB", kb);
}

/* ---------------------------------------------------------------- graphs */

/* one graph: series a (and b, if given) in percent of top over the history */
static void draw_graph(Widget w, const double *a, Pixel ca, const double *b, Pixel cb,
                       int stacked, double top_value)
{
    Display *d = XtDisplay(w);
    Window win = XtWindow(w);
    Dimension W = 0, H = 0;
    int i, x0, y0, gw, gh;

    if (!win)
        return;
    XtVaGetValues(w, XmNwidth, &W, XmNheight, &H, NULL);
    x0 = 4;
    y0 = 4;
    gw = W - 8;
    gh = H - 8;
    if (gw < 10 || gh < 10)
        return;
    XSetForeground(d, gc, c_bg);
    XFillRectangle(d, win, gc, 0, 0, W, H);
    XSetForeground(d, gc, c_grid);
    for (i = 0; i <= 4; i++)
        XDrawLine(d, win, gc, x0, y0 + gh * i / 4, x0 + gw, y0 + gh * i / 4);
    for (i = 0; i <= 6; i++)
        XDrawLine(d, win, gc, x0 + gw * i / 6, y0, x0 + gw * i / 6, y0 + gh);
    if (top_value <= 0)
        top_value = 1;
    for (i = 1; i < nhist; i++) {
        int xa = x0 + gw * (NHIST - nhist + i - 1) / (NHIST - 1);
        int xb = x0 + gw * (NHIST - nhist + i) / (NHIST - 1);
        double va = a[i - 1], vb = a[i];
        int ya, yb;

        if (stacked && b) {
            /* b under a: the line of a sits on b */
            double wa = b[i - 1] + va, wb = b[i] + vb;
            int pa = y0 + gh - (int)(gh * (b[i - 1] > top_value ? top_value : b[i - 1]) / top_value);
            int pb = y0 + gh - (int)(gh * (b[i] > top_value ? top_value : b[i]) / top_value);

            XSetForeground(d, gc, cb);
            XDrawLine(d, win, gc, xa, pa, xb, pb);
            va = wa;
            vb = wb;
        } else if (b) {
            int pa = y0 + gh - (int)(gh * (b[i - 1] > top_value ? top_value : b[i - 1]) / top_value);
            int pb = y0 + gh - (int)(gh * (b[i] > top_value ? top_value : b[i]) / top_value);

            XSetForeground(d, gc, cb);
            XDrawLine(d, win, gc, xa, pa, xb, pb);
        }
        ya = y0 + gh - (int)(gh * (va > top_value ? top_value : va) / top_value);
        yb = y0 + gh - (int)(gh * (vb > top_value ? top_value : vb) / top_value);
        XSetForeground(d, gc, ca);
        XDrawLine(d, win, gc, xa, ya, xb, yb);
    }
}

static double net_top(void)
{
    double m = 10;
    int i;

    for (i = 0; i < nhist; i++) {
        if (h_in[i] > m)
            m = h_in[i];
        if (h_out[i] > m)
            m = h_out[i];
    }
    return m * 1.1;
}

static void draw_graphs(void)
{
    draw_graph(gcpu, h_kern, c_kern, h_user, c_user, 1, 100);
    draw_graph(gmem, h_mem, c_mem, h_swap, c_swap, 0, 100);
    draw_graph(gnet, h_in, c_in, h_out, c_out, 0, net_top());
}

static void expose_graph(Widget w, XtPointer cd, XtPointer cb)
{
    draw_graphs();
}

/* ---------------------------------------------------------------- file systems */

static void draw_fs(void)
{
    Display *d = XtDisplay(fsarea);
    Window win = XtWindow(fsarea);
    Dimension W = 0, H = 0;
    int i, y, lh, bx, bw;
    char line[200], t[16], f[16], a[16];

    if (!win)
        return;
    XtVaGetValues(fsarea, XmNwidth, &W, XmNheight, &H, NULL);
    lh = font->ascent + font->descent + 10;
    XClearWindow(d, win);
    XSetForeground(d, gc, c_text);
    y = 8 + font->ascent;
    sprintf(line, "%-20s %-12s %-6s %10s %10s %10s", "Device", "Directory", "Type", "Total", "Free", "Available");
    XDrawString(d, win, gc, 8, y, line, (int)strlen(line));
    bx = 8 + XTextWidth(font, line, (int)strlen(line)) + 16;
    bw = (int)W - bx - 12;
    for (i = 0; i < nfs; i++) {
        double used = fs[i].total > 0 ? (double)(fs[i].total - fs[i].free) / fs[i].total : 0;

        y += lh;
        human(t, fs[i].total);
        human(f, fs[i].free);
        human(a, fs[i].avail);
        sprintf(line, "%-20.20s %-12.12s %-6.6s %10s %10s %10s", fs[i].dev, fs[i].dir, fs[i].type, t, f, a);
        XSetForeground(d, gc, c_text);
        XDrawString(d, win, gc, 8, y, line, (int)strlen(line));
        if (bw > 40) {
            int bh = font->ascent + 2, by = y - font->ascent;
            char pc[8];

            XSetForeground(d, gc, c_grid);
            XFillRectangle(d, win, gc, bx, by, bw, bh);
            XSetForeground(d, gc, used > 0.9 ? c_out : c_bar);
            XFillRectangle(d, win, gc, bx, by, (int)(bw * used), bh);
            sprintf(pc, "%.0f %%", used * 100);
            XSetForeground(d, gc, c_text);
            XDrawString(d, win, gc, bx + bw + 4 - XTextWidth(font, pc, (int)strlen(pc)) - 8, y, pc, (int)strlen(pc));
        }
    }
}

static void expose_fs(Widget w, XtPointer cd, XtPointer cb)
{
    draw_fs();
}

/* ---------------------------------------------------------------- processes */

static int cmp_rows(const void *a, const void *b)
{
    const struct row *x = a, *y = b;

    switch (sortby) {
    case SORT_MEM:
        return x->p.rss < y->p.rss ? 1 : x->p.rss > y->p.rss ? -1 : 0;
    case SORT_PID:
        return x->p.pid < y->p.pid ? -1 : x->p.pid > y->p.pid;
    case SORT_NAME:
        return strcmp(x->p.fname, y->p.fname);
    default:
        if (x->pcpu != y->pcpu)
            return x->pcpu < y->pcpu ? 1 : -1;
        return x->p.rss < y->p.rss ? 1 : x->p.rss > y->p.rss ? -1 : 0;
    }
}

static void show_procs(void)
{
    XmString *items;
    int i, n = 0, sel = -1, *pos = NULL, npos = 0;
    pid_t keep = -1;
    uid_t me = getuid();
    char line[200], rss[16], vsz[16], st[64];

    if (XmListGetSelectedPos(plist, &pos, &npos) && npos > 0 && shown && pos[0] - 1 < nrows)
        keep = shown[pos[0] - 1];
    if (pos)
        XtFree((char *)pos);
    qsort(rows, nrows, sizeof *rows, cmp_rows);
    items = (XmString *)XtMalloc((nrows + 1) * sizeof *items);
    shown = realloc(shown, (nrows + 1) * sizeof *shown);
    for (i = 0; i < nrows; i++) {
        const struct ast_proc *p = &rows[i].p;
        long t = (long)(p->cpu / 1000);

        if (only_mine && p->uid != me)
            continue;
        human(rss, p->rss);
        human(vsz, p->size);
        sprintf(line, "%6ld %-8s %c %5.1f %9s %9s %4ld:%02ld  %.60s", (long)p->pid, user_name(p->uid),
                p->state, rows[i].pcpu, rss, vsz, t / 60, t % 60, p->args[0] ? p->args : p->fname);
        items[n] = XmStringCreateLocalized(line);
        if (p->pid == keep)
            sel = n;
        shown[n++] = p->pid;
    }
    XtVaSetValues(plist, XmNitems, items, XmNitemCount, n, NULL);
    for (i = 0; i < n; i++)
        XmStringFree(items[i]);
    XtFree((char *)items);
    if (sel >= 0)
        XmListSelectPos(plist, sel + 1, False);
    sprintf(st, "%d processes%s", n, only_mine ? " (yours)" : getuid() ? " (only yours can be read)" : "");
    set_label(pstatus, st);
}

/* the processes again, each one's share of the interval since the last time */
static void sample_procs(const struct ast_cpu *c)
{
    struct ast_proc *cur;
    int n = ast_procs(&cur), i, j;
    double t = c->hz ? (double)c->lbolt / c->hz : 0, dt = t - prev_t, ssum = 0, f = 1;

    if (n < 0)
        return;
    rows = realloc(rows, (n + 1) * sizeof *rows);
    /* the system times of the interval, to scale to the kernel's real share */
    for (i = 0; i < n; i++) {
        for (j = 0; j < nprev && prev[j].pid != cur[i].pid; j++)
            ;
        if (j < nprev && cur[i].stime > prev[j].stime)
            ssum += (cur[i].stime - prev[j].stime) / 1000.0;
    }
    if (ssum > 0 && dt > 0)
        f = kern_share * dt / ssum;
    if (f > 1)
        f = 1;
    for (i = 0; i < n; i++) {
        double d = 0;

        for (j = 0; j < nprev && prev[j].pid != cur[i].pid; j++)
            ;
        if (j < nprev && dt > 0) {
            double du = (long)(cur[i].utime - prev[j].utime) / 1000.0;
            double ds = (long)(cur[i].stime - prev[j].stime) / 1000.0;

            d = 100 * ((du > 0 ? du : 0) + (ds > 0 ? ds * f : 0)) / dt;
        }
        rows[i].p = cur[i];
        rows[i].pcpu = d > 100 ? 100 : d;
    }
    nrows = n;
    free(prev);
    prev = cur;
    nprev = n;
    prev_t = t;
}

static void sort_cb(Widget w, XtPointer cd, XtPointer cb)
{
    if (((XmToggleButtonCallbackStruct *)cb)->set) {
        sortby = (int)(long)cd;
        show_procs();
    }
}

static void mine_cb(Widget w, XtPointer cd, XtPointer cb)
{
    only_mine = ((XmToggleButtonCallbackStruct *)cb)->set;
    show_procs();
}

static pid_t selected_pid(void)
{
    int *pos = NULL, npos = 0;
    pid_t pid = -1;

    if (XmListGetSelectedPos(plist, &pos, &npos) && npos > 0 && shown)
        pid = shown[pos[0] - 1];
    if (pos)
        XtFree((char *)pos);
    return pid;
}

static pid_t sig_pid;
static int sig_num;

static void do_signal(Widget w, XtPointer cd, XtPointer cb)
{
    char m[120];

    if (sig_pid > 0 && kill(sig_pid, sig_num) < 0)
        sprintf(m, "Process %ld: %s", (long)sig_pid, strerror(errno));
    else
        sprintf(m, "Process %ld: %s sent", (long)sig_pid, sig_num == SIGKILL ? "kill" : "end");
    set_label(status, m);
}

static void ask_signal(Widget w, XtPointer cd, XtPointer cb)
{
    char m[200];
    XmString x;
    int i;

    sig_num = (int)(long)cd;
    if ((sig_pid = selected_pid()) <= 0) {
        set_label(status, "Select a process first");
        return;
    }
    for (i = 0; i < nrows && rows[i].p.pid != sig_pid; i++)
        ;
    sprintf(m, "%s process %ld (%s)?\n%s", sig_num == SIGKILL ? "Kill" : "End", (long)sig_pid,
            i < nrows ? rows[i].p.fname : "?",
            sig_num == SIGKILL ? "Killing a process loses what it has not saved." :
                                 "The process is asked to stop (SIGTERM).");
    x = XmStringCreateLtoR(m, XmFONTLIST_DEFAULT_TAG);
    XtVaSetValues(confirm, XmNmessageString, x, NULL);
    XmStringFree(x);
    XtManageChild(confirm);
}

/* ---------------------------------------------------------------- Z3660 */

static void show_ztop(void)
{
    static char buf[8192];
    FILE *p;
    int n = 0, c;

    if (access(ZTOP, X_OK) < 0) {
        XmTextSetString(ztext, "ztop is not installed (" ZTOP ").");
        return;
    }
    if (!(p = popen(ZTOP " 2>&1", "r")))
        return;
    while (n < (int)sizeof buf - 1 && (c = getc(p)) != EOF)
        buf[n++] = (char)c;
    buf[n] = 0;
    while (getc(p) != EOF)
        ;
    pclose(p);
    XmTextSetString(ztext, buf);
}

/* ---------------------------------------------------------------- sampling */

static void sample(XtPointer data, XtIntervalId *id)
{
    struct ast_cpu c;
    struct ast_if *ifs;
    int ni, i, tab = current_tab();
    char m[200], a[16], b[16], s1[16], s2[16];

    tick++;
    if (ast_cpu(&c) == 0 && c.prf) {
        if (have_cpu0) {
            double u = c.s_user - cpu0.s_user, k = c.s_kernel - cpu0.s_kernel, d = c.s_idle - cpu0.s_idle;
            double all = u + k + d;

            if (all > 0) {
                push(h_user, 100 * u / all);
                push(h_kern, 100 * k / all);
                kern_share = k / all;
            }
        }
        cpu0 = c;
        have_cpu0 = 1;
    } else {
        push(h_user, 0);
        push(h_kern, 0);
    }
    if (ast_mem(&mem) == 0) {
        push(h_mem, mem.total > 0 ? 100.0 * (mem.total - mem.free) / mem.total : 0);
        push(h_swap, mem.swap_total > 0 ? 100.0 * (mem.swap_total - mem.swap_free) / mem.swap_total : 0);
    }
    ni = ast_ifs(&ifs);
    for (i = 0; i < ni && strncmp(ifs[i].name, "lo", 2) == 0; i++)
        ;
    if (i < ni) {
        strcpy(netname, ifs[i].name);
        push(h_in, have_net0 && ifs[i].ipackets >= net_in0 ? (double)(ifs[i].ipackets - net_in0) : 0);
        push(h_out, have_net0 && ifs[i].opackets >= net_out0 ? (double)(ifs[i].opackets - net_out0) : 0);
        net_in0 = ifs[i].ipackets;
        net_out0 = ifs[i].opackets;
        have_net0 = 1;
    } else {
        push(h_in, 0);
        push(h_out, 0);
    }
    free(ifs);
    if (nhist < NHIST)
        nhist++;

    if (c.prf)
        sprintf(m, "CPU   user %.0f %%   system %.0f %%   idle %.0f %%", h_user[nhist - 1], h_kern[nhist - 1],
                100 - h_user[nhist - 1] - h_kern[nhist - 1]);
    else
        sprintf(m, "CPU   unknown: the kernel profiler (/dev/prf) is in use for something else");
    set_label(lcpu, m);
    human(a, mem.total - mem.free);
    human(b, mem.total);
    human(s1, mem.swap_total - mem.swap_free);
    human(s2, mem.swap_total);
    sprintf(m, "Memory   %s of %s (%.0f %%)        Swap   %s of %s (%.0f %%)", a, b, h_mem[nhist - 1], s1, s2,
            h_swap[nhist - 1]);
    set_label(lmem, m);
    sprintf(m, "Network %s   received %.0f packets/s   sent %.0f packets/s", netname, h_in[nhist - 1],
            h_out[nhist - 1]);
    set_label(lnet, m);
    if (tab == TAB_RES)
        draw_graphs();

    if (tick % 3 == 1) {
        sample_procs(&c);
        if (tab == TAB_PROCS)
            show_procs();
    }
    if (tick % 5 == 1) {
        free(fs);
        nfs = ast_filesystems(&fs);
        if (nfs < 0)
            nfs = 0;
        if (tab == TAB_FS)
            draw_fs();
    }
    if (tab == TAB_Z3660 && tick % 2 == 1)
        show_ztop();
    XtAppAddTimeOut(app, 1000, sample, NULL);
}

static void page_cb(Widget w, XtPointer cd, XtPointer cb)
{
    switch (current_tab()) {
    case TAB_PROCS: show_procs(); break;
    case TAB_RES:   draw_graphs(); break;
    case TAB_FS:    draw_fs(); break;
    case TAB_Z3660: show_ztop(); break;
    }
}

/* ---------------------------------------------------------------- building */

static Widget tab(const char *name, int page)
{
    Widget f = XtVaCreateManagedWidget(name, xmFormWidgetClass, nb,
                                       XmNpageNumber, page, XmNmarginWidth, 8, XmNmarginHeight, 8, NULL);
    XmString x = XmStringCreateLocalized((char *)name);

    XtVaCreateManagedWidget(name, xmPushButtonWidgetClass, nb, XmNnotebookChildType, XmMAJOR_TAB,
                            XmNpageNumber, page, XmNlabelString, x, NULL);
    XmStringFree(x);
    return f;
}

static Widget graph(Widget parent, Widget above, Widget *label, const char *legend)
{
    Widget fr, g, l;
    XmString x = XmStringCreateLocalized((char *)legend);

    l = XtVaCreateManagedWidget("legend", xmLabelWidgetClass, parent, XmNlabelString, x,
                                XmNalignment, XmALIGNMENT_BEGINNING,
                                XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM, XmNtopWidget, above,
                                XmNtopOffset, above ? 8 : 0,
                                XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    XmStringFree(x);
    fr = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, parent, XmNshadowType, XmSHADOW_IN,
                                 XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l,
                                 XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    g = XtVaCreateManagedWidget("graph", xmDrawingAreaWidgetClass, fr, XmNheight, 110, XmNbackground, c_bg, NULL);
    XtAddCallback(g, XmNexposeCallback, expose_graph, NULL);
    XtAddCallback(g, XmNresizeCallback, expose_graph, NULL);
    *label = l;
    return g;
}

static void build_procs(Widget f)
{
    Widget bar, rb, b, hdr, end, kil, mine;
    XmString x;
    Arg a[12];
    int n;
    static const char *const sorts[] = { "CPU", "Memory", "PID", "Name" };
    char h[200];

    bar = XtVaCreateManagedWidget("bar", xmRowColumnWidgetClass, f, XmNorientation, XmHORIZONTAL,
                                  XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, NULL);
    x = XmStringCreateLocalized("Sort by:");
    XtVaCreateManagedWidget("sortl", xmLabelWidgetClass, bar, XmNlabelString, x, NULL);
    XmStringFree(x);
    rb = XmCreateRadioBox(bar, "sort", NULL, 0);
    XtVaSetValues(rb, XmNorientation, XmHORIZONTAL, NULL);
    for (n = 0; n < 4; n++) {
        x = XmStringCreateLocalized((char *)sorts[n]);
        b = XtVaCreateManagedWidget(sorts[n], xmToggleButtonWidgetClass, rb, XmNlabelString, x,
                                    XmNset, n == SORT_CPU, NULL);
        XmStringFree(x);
        XtAddCallback(b, XmNvalueChangedCallback, sort_cb, (XtPointer)(long)n);
    }
    XtManageChild(rb);
    x = XmStringCreateLocalized("Only my processes");
    mine = XtVaCreateManagedWidget("mine", xmToggleButtonWidgetClass, bar, XmNlabelString, x, NULL);
    XmStringFree(x);
    XtAddCallback(mine, XmNvalueChangedCallback, mine_cb, NULL);

    x = XmStringCreateLocalized("End Process");
    end = XtVaCreateManagedWidget("end", xmPushButtonWidgetClass, f, XmNlabelString, x,
                                  XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    XmStringFree(x);
    x = XmStringCreateLocalized("Kill Process");
    kil = XtVaCreateManagedWidget("kill", xmPushButtonWidgetClass, f, XmNlabelString, x,
                                  XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_WIDGET,
                                  XmNrightWidget, end, XmNrightOffset, 8, NULL);
    XmStringFree(x);
    XtAddCallback(end, XmNactivateCallback, ask_signal, (XtPointer)(long)SIGTERM);
    XtAddCallback(kil, XmNactivateCallback, ask_signal, (XtPointer)(long)SIGKILL);
    pstatus = XtVaCreateManagedWidget("pstatus", xmLabelWidgetClass, f, XmNalignment, XmALIGNMENT_BEGINNING,
                                      XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                                      XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, kil, NULL);

    sprintf(h, "%6s %-8s %c %5s %9s %9s %7s  %s", "PID", "User", 'S', "CPU%", "Memory", "Virtual", "Time", "Command");
    x = XmStringCreateLocalized(h);
    hdr = XtVaCreateManagedWidget("header", xmLabelWidgetClass, f, XmNlabelString, x,
                                  XmNalignment, XmALIGNMENT_BEGINNING, XmNtopAttachment, XmATTACH_WIDGET,
                                  XmNtopWidget, bar, XmNtopOffset, 6, XmNleftAttachment, XmATTACH_FORM,
                                  XmNleftOffset, 4, XmNrightAttachment, XmATTACH_FORM, NULL);
    XmStringFree(x);
    n = 0;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(a[n], XmNvisibleItemCount, 18); n++;
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNtopWidget, hdr); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNbottomWidget, end); n++;
    XtSetArg(a[n], XmNbottomOffset, 6); n++;
    plist = XmCreateScrolledList(f, "processes", a, n);
    XtManageChild(plist);
}

static void build_res(Widget f)
{
    gcpu = graph(f, NULL, &lcpu, "CPU");
    gmem = graph(f, XtParent(gcpu), &lmem, "Memory and Swap");
    gnet = graph(f, XtParent(gmem), &lnet, "Network");
}

int main(int argc, char **argv)
{
    Widget main_form, f;
    char err[200];
    XmFontList fl;
    XGCValues gv;
    int fail;

    top = XtVaOpenApplication(&app, "Amixsysmon", NULL, 0, &argc, argv, NULL, sessionShellWidgetClass,
                              XmNtitle, "System Monitor", XmNwidth, 760, XmNheight, 600, NULL);
    fail = ast_open(err, sizeof err);
    font = XLoadQueryFont(XtDisplay(top), "-*-courier-medium-r-normal--12-*-*-*-*-*-*-*");
    if (!font)
        font = XLoadQueryFont(XtDisplay(top), "fixed");
    fl = XmFontListCreate(font, XmFONTLIST_DEFAULT_TAG);

    c_text = BlackPixelOfScreen(XtScreen(top));
    c_bg = colour("#1E2430", c_text);
    c_grid = colour("#3A4252", WhitePixelOfScreen(XtScreen(top)));
    c_user = colour("#4E9BE8", c_grid);
    c_kern = colour("#E8A33D", c_grid);
    c_mem = colour("#9B6BD6", c_grid);
    c_swap = colour("#3DBF8A", c_grid);
    c_in = colour("#4E9BE8", c_grid);
    c_out = colour("#E8603D", c_grid);
    c_bar = colour("#4E9BE8", c_grid);
    c_barbg = colour("#D8D8D8", c_grid);

    main_form = XtVaCreateManagedWidget("main", xmFormWidgetClass, top, NULL);
    status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, main_form, XmNalignment, XmALIGNMENT_BEGINNING,
                                     XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                                     XmNrightAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
    nb = XtVaCreateManagedWidget("notebook", xmNotebookWidgetClass, main_form, XmNbindingType, XmNONE,
                                 XmNbackPagePlacement, XmTOP_RIGHT, XmNorientation, XmVERTICAL,
                                 XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                                 XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_WIDGET,
                                 XmNbottomWidget, status, NULL);
    XtAddCallback(nb, XmNpageChangedCallback, page_cb, NULL);

    f = tab("Processes", TAB_PROCS);
    build_procs(f);
    XtVaSetValues(plist, XmNfontList, fl, NULL);
    XtVaSetValues(XtNameToWidget(f, "header"), XmNfontList, fl, NULL);

    f = tab("Resources", TAB_RES);
    build_res(f);

    f = tab("File Systems", TAB_FS);
    fsarea = XtVaCreateManagedWidget("filesystems", xmDrawingAreaWidgetClass, f,
                                     XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                                     XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    XtAddCallback(fsarea, XmNexposeCallback, expose_fs, NULL);
    XtAddCallback(fsarea, XmNresizeCallback, expose_fs, NULL);

    f = tab("Z3660", TAB_Z3660);
    {
        Arg a[8];
        int n = 0;

        XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
        XtSetArg(a[n], XmNeditable, False); n++;
        XtSetArg(a[n], XmNcursorPositionVisible, False); n++;
        XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
        XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
        XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
        XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
        ztext = XmCreateScrolledText(f, "ztop", a, n);
        XtManageChild(ztext);
        XtVaSetValues(ztext, XmNfontList, fl, NULL);
    }

    confirm = XmCreateQuestionDialog(top, "confirm", NULL, 0);
    XtVaSetValues(XtParent(confirm), XmNtitle, "System Monitor", NULL);
    XtUnmanageChild(XmMessageBoxGetChild(confirm, XmDIALOG_HELP_BUTTON));
    XtAddCallback(confirm, XmNokCallback, do_signal, NULL);

    XtRealizeWidget(top);
    gv.font = font->fid;
    gc = XCreateGC(XtDisplay(top), XtWindow(top), GCFont, &gv);
    XtVaSetValues(fsarea, XmNbackground, c_barbg, NULL);
    if (fail < 0)
        set_label(status, err);
    else {
        char m[200];

        sprintf(m, "Kernel %s", ast_kernel());
        set_label(status, m);
    }
    sample(NULL, NULL);
    XtAppMainLoop(app);
    return 0;
}
