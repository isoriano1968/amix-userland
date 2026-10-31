/*
 * ztopcde.c -- the Z3660 in a CDE window on AMIX: the board's state (ztop,
 * every 2 s) and the programs for its second ARM core (z3660ARM_Core_*.bin
 * in /root/z3660ARMCores, listed by their full names), with armbench:
 *   Load     armbench -load    the first program after power-on (core 0's loader, once)
 *   Reload   armbench -reload  the running program copies the new one over itself
 * Both say so when the other one is the right one.
 * The ARM clock is shown; setting it needs firmware support (not there yet).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Text.h>

#define CORES_DIR   "/root/z3660ARMCores"
#define ARMBENCH    CORES_DIR "/armbench"
#define ZTOP        "/usr/amiga/bin/ztop"
#define PREFIX      "z3660ARM_Core_"

static XtAppContext app;
static Widget board, list, status, freq;

/* a command's output into buf (stdout and stderr) */
static void run(const char *cmd, char *buf, int len)
{
    FILE *p = popen(cmd, "r");
    int n = 0, c;

    buf[0] = 0;
    if (!p)
        return;
    while (n < len - 1 && (c = getc(p)) != EOF)
        buf[n++] = (char)c;
    buf[n] = 0;
    while (getc(p) != EOF)
        ;
    pclose(p);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = 0;
}

static void say(const char *s)
{
    XmString x = XmStringCreateLtoR((char *)s, XmFONTLIST_DEFAULT_TAG);

    XtVaSetValues(status, XmNlabelString, x, NULL);
    XmStringFree(x);
}

static void refresh(XtPointer data, XtIntervalId *id)
{
    static char buf[4096];
    char *mhz;

    (void)data; (void)id;
    run(ZTOP " 2>&1", buf, sizeof buf);
    XmTextSetString(board, buf);
    /* the ARM clock as ztop reports it for core 1 ("..., 666 MHz") */
    if ((mhz = strstr(buf, "ARM core 1")) == NULL || (mhz = strstr(mhz, " MHz")) == NULL) {
        /* no program on core 1 to measure it: the boot loader's setting (the
           firmware never changes it) */
        XmString x = XmStringCreateLocalized("ARM clock: 666 MHz (standard; measured once a program runs on core 1)");

        XtVaSetValues(freq, XmNlabelString, x, NULL);
        XmStringFree(x);
    } else {
        char *b = mhz;
        char lab[64];
        XmString x;

        while (b > buf && b[-1] >= '0' && b[-1] <= '9')
            b--;
        sprintf(lab, "ARM clock: %.*s MHz (setting it needs firmware support)", (int)(mhz - b), b);
        x = XmStringCreateLocalized(lab);
        XtVaSetValues(freq, XmNlabelString, x, NULL);
        XmStringFree(x);
    }
    XtAppAddTimeOut(app, 2000, refresh, NULL);
}

static void fill_list(void)
{
    DIR *d = opendir(CORES_DIR);
    struct dirent *e;

    XmListDeleteAllItems(list);
    if (!d) {
        say("no " CORES_DIR);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        int n = (int)strlen(e->d_name);

        if (!strncmp(e->d_name, PREFIX, strlen(PREFIX)) && n > (int)strlen(PREFIX)) {
            XmString x = XmStringCreateLocalized(e->d_name);

            XmListAddItemUnselected(list, x, 0);
            XmStringFree(x);
        }
    }
    closedir(d);
}

/* the selected program's file name, or NULL */
static char *selected(void)
{
    static char path[512];
    XmString *items;
    int n;
    char *s;

    XtVaGetValues(list, XmNselectedItems, &items, XmNselectedItemCount, &n, NULL);
    if (n < 1 || !(s = (char *)XmStringUnparse(items[0], NULL, XmCHARSET_TEXT, XmCHARSET_TEXT, NULL, 0, XmOUTPUT_ALL))) {
        say("select a program first");
        return NULL;
    }
    sprintf(path, "%s/%s", CORES_DIR, s);
    XtFree(s);
    return path;
}

/* how many times core 0's loader started core 1 since power-on, -1 if unknown */
static int loads(void)
{
    char out[2048], *p;

    run(ARMBENCH " -dump 2>&1", out, sizeof out);
    return (p = strstr(out, "loads ")) != NULL ? atoi(p + 6) : -1;
}

static void action(Widget w, XtPointer how, XtPointer call)
{
    char cmd[700], out[2048], *f;
    int reload = !strcmp((char *)how, "-reload"), n;

    (void)w; (void)call;
    if (!(f = selected()))
        return;
    n = loads();
    if (!reload && n > 0) {
        say("core 1 has been started since power-on: use Reload (or switch off and on)");
        return;
    }
    if (reload && n == 0) {
        say("nothing runs on core 1 yet: use Load");
        return;
    }
    say(reload ? "reloading..." : "loading...");
    XmUpdateDisplay(status);
    sprintf(cmd, "cd %s && %s %s %s 2>&1", CORES_DIR, ARMBENCH, (char *)how, f);
    run(cmd, out, sizeof out);
    say(out[0] ? out : "(no answer)");
    refresh(NULL, NULL);
}

static void rescan(Widget w, XtPointer d, XtPointer c)
{
    (void)w; (void)d; (void)c;
    fill_list();
    say("list read again");
}

static void quit(Widget w, XtPointer d, XtPointer c)
{
    (void)w; (void)d; (void)c;
    exit(0);
}

static Widget button(Widget parent, const char *name, XtCallbackProc cb, XtPointer data)
{
    Widget b = XtVaCreateManagedWidget(name, xmPushButtonWidgetClass, parent, NULL);

    XtAddCallback(b, XmNactivateCallback, cb, data);
    return b;
}

int main(int argc, char **argv)
{
    Widget top, form, f1, f2, rc, row;
    Arg a[8];
    int n;

    top = XtVaAppInitialize(&app, "Ztopcde", NULL, 0, &argc, argv, NULL,
                            XmNtitle, "Z3660", XmNiconName, "Z3660", NULL);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top,
                                   XmNmarginWidth, 8, XmNmarginHeight, 8, XmNverticalSpacing, 6, NULL);

    f1 = XtVaCreateManagedWidget("boardFrame", xmFrameWidgetClass, form,
                                 XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                                 XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaCreateManagedWidget("Board", xmLabelWidgetClass, f1, XmNchildType, XmFRAME_TITLE_CHILD, NULL);
    n = 0;
    XtSetArg(a[n], XmNeditable, False); n++;
    XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
    XtSetArg(a[n], XmNrows, 6); n++;
    XtSetArg(a[n], XmNcolumns, 66); n++;
    XtSetArg(a[n], XmNcursorPositionVisible, False); n++;
    XtSetArg(a[n], XmNtraversalOn, False); n++;
    board = XmCreateText(f1, "board", a, n);
    XtManageChild(board);

    f2 = XtVaCreateManagedWidget("coreFrame", xmFrameWidgetClass, form,
                                 XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, f1,
                                 XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaCreateManagedWidget("ARM core 1 programs (" CORES_DIR ")", xmLabelWidgetClass, f2,
                            XmNchildType, XmFRAME_TITLE_CHILD, NULL);
    rc = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, f2, XmNorientation, XmVERTICAL, NULL);
    n = 0;
    XtSetArg(a[n], XmNvisibleItemCount, 5); n++;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    list = XmCreateScrolledList(rc, "list", a, n);
    XtManageChild(list);
    row = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, rc, XmNorientation, XmHORIZONTAL, NULL);
    button(row, "Load (after power-on)", action, (XtPointer)"-load");
    button(row, "Reload (replace running)", action, (XtPointer)"-reload");
    button(row, "Read list again", rescan, NULL);
    freq = XtVaCreateManagedWidget("ARM clock: ?", xmLabelWidgetClass, rc, NULL);
    status = XtVaCreateManagedWidget(" ", xmLabelWidgetClass, form,
                                     XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, f2,
                                     XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                                     XmNalignment, XmALIGNMENT_BEGINNING, NULL);
    row = XtVaCreateManagedWidget("quitRow", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
                                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, status,
                                  XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);
    button(row, "Close", quit, NULL);

    XtRealizeWidget(top);
    fill_list();
    refresh(NULL, NULL);
    XtAppMainLoop(app);
    return 0;
}
