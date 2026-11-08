/*
 * amixq3 -- Quake III: the CDE front end of Quake III Arena (ioquake3) on
 * the Z3660's ARM core 1 (games/ioquake3, z3660ARM_Core_q3.bin).  Choose
 * the picture's size and the map, then Start: c1q3 loads the game into core
 * 1 and opens the game's window, over which core 1 draws (the RTG overlay);
 * sound comes from HDMI.  F10 or closing the game's window quits the game,
 * Stop here too (the game writes its config back first).
 *
 * The board is c1q3's (root, /dev/zz9000); amixq3 runs it and shows its
 * status lines.  The game data is /opt/ioq3/demoq3 (the demo), the player's
 * files ~/.q3a (config, screenshots).  The setup is kept in ~/.amixq3; the
 * messages of c1q3 and of the engine on core 1 in ~/.q3a/amixq3.log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <Xm/PushB.h>
#include <Xm/ComboBox.h>
#include <Xm/Separator.h>

#define C1Q3        "/opt/amix/bin/c1q3"

static const char *sizes[] = { "320x240", "400x300", "512x384", "640x480", "800x600" };
#define NSIZES      5
static const char *maps[] = { "Main menu", "q3dm1", "q3dm7", "q3dm17", "q3tourney2" };
#define NMAPS       5

static XtAppContext app;
static Widget top, size_menu, size_btn[NSIZES], cb_map, tf_extra, status, b_start, b_stop;
static int size_i = 3;
static pid_t child = -1;
static int from_c1 = -1;
static XtInputId in_id;
static char home[1024];

static void say(const char *s)
{
    XmString x = XmStringCreateLocalized((char *)s);

    XtVaSetValues(status, XmNlabelString, x, NULL);
    XmStringFree(x);
}

/* --- the setup: ~/.amixq3, key=value lines --- */
static char *setup_path(void)
{
    static char p[1100];

    sprintf(p, "%.1000s/.amixq3", getenv("HOME") ? getenv("HOME") : ".");
    return p;
}

static void setup_load(void)
{
    FILE *f = fopen(setup_path(), "r");
    char line[600];
    int i;

    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        char *v = strchr(line, '='), *nl = strchr(line, '\n');

        if (nl)
            *nl = 0;
        if (!v)
            continue;
        *v++ = 0;
        if (!strcmp(line, "size")) {
            for (i = 0; i < NSIZES; i++)
                if (!strcmp(v, sizes[i]))
                    size_i = i;
        } else if (!strcmp(line, "map"))
            XmComboBoxSetItem(cb_map, XmStringCreateLocalized(v)),
            XmTextFieldSetString(XtNameToWidget(cb_map, "*Text"), v);
        else if (!strcmp(line, "extra"))
            XmTextFieldSetString(tf_extra, v);
    }
    fclose(f);
    XtVaSetValues(size_menu, XmNmenuHistory, size_btn[size_i], NULL);
}

static char *map_text(void)
{
    Widget t = XtNameToWidget(cb_map, "*Text");

    return t ? XmTextFieldGetString(t) : XtNewString("Main menu");
}

static void setup_save(void)
{
    FILE *f = fopen(setup_path(), "w");
    char *m = map_text(), *e = XmTextFieldGetString(tf_extra);

    if (f) {
        fprintf(f, "size=%s\nmap=%s\nextra=%s\n", sizes[size_i], m, e);
        fclose(f);
    }
    XtFree(m);
    XtFree(e);
}

/* --- c1q3 --- */
static void stopped(void)
{
    int st;

    if (in_id) {
        XtRemoveInput(in_id);
        in_id = 0;
    }
    if (from_c1 >= 0) {
        close(from_c1);
        from_c1 = -1;
    }
    if (child > 0 && waitpid(child, &st, 0) == child && WIFEXITED(st)) {
        if (WEXITSTATUS(st) == 0)
            say("The game has ended.");
        else if (WEXITSTATUS(st) == 3)
            say("Core 1 runs a game already (another window has it).");
        else if (WEXITSTATUS(st) == 127)
            say("c1q3 is not installed (games/ioquake3).");
        else
            say("The game stopped with an error: see ~/.q3a/amixq3.log.");
    }
    child = -1;
    XtSetSensitive(b_start, True);
    XtSetSensitive(b_stop, False);
}

static void from_c1q3(XtPointer cd, int *src, XtInputId *id)
{
    static char buf[512];
    static int n;
    int r;

    (void)cd; (void)id;
    r = (int)read(*src, buf + n, sizeof(buf) - 1 - n);
    if (r <= 0) {
        stopped();
        return;
    }
    n += r;
    buf[n] = 0;
    for (;;) {
        char *nl = strchr(buf, '\n'), s[200];
        unsigned long frames, avg, max, under, heap, st;
        double mb, secs;
        int files;

        if (!nl)
            break;
        *nl = 0;
        if (sscanf(buf, "status %lu %lu %lu %lu %lu %lu", &frames, &avg, &max, &under, &heap, &st) == 6) {
            if (!frames)
                sprintf(s, "Starting the engine on core 1 ...");
            else
                sprintf(s, "Running: %.1f fps (%lu ms a frame, longest %lu), sound gaps %lu, core 1 memory left %lu MB",
                        avg ? 1000.0 / avg : 0.0, avg, max, under, heap);
            say(s);
        } else if (sscanf(buf, "c1q3: %d files, %lf MB in %lf s", &files, &mb, &secs) == 3) {
            sprintf(s, "Game data in core 1 (%.0f MB in %.0f s); the engine starts ...", mb, secs);
            say(s);
        }
        n -= (int)(nl + 1 - buf);
        memmove(buf, nl + 1, (size_t)n + 1);
    }
    if (n >= (int)sizeof(buf) - 1)
        n = 0;
}

static void start(Widget w, XtPointer cd, XtPointer cb)
{
    static char ws[8], hs[8], extra_copy[512];
    char *argv[48], *m, *e, *tok;
    int from[2], argc = 0;

    (void)w; (void)cd; (void)cb;
    if (child > 0)
        return;
    setup_save();
    if (pipe(from) < 0) {
        say("No pipe.");
        return;
    }
    sscanf(sizes[size_i], "%7[0-9]x%7[0-9]", ws, hs);
    argv[argc++] = C1Q3;
    argv[argc++] = "-v";                /* the engine's console: into the log */
    argv[argc++] = "-width";
    argv[argc++] = ws;
    argv[argc++] = "-height";
    argv[argc++] = hs;
    argv[argc++] = "-home";
    argv[argc++] = home;
    m = map_text();
    if (*m && strcmp(m, "Main menu") && !strchr(m, ' ')) {
        static char map[128];

        strncpy(map, m, sizeof(map) - 1);
        argv[argc++] = "+map";
        argv[argc++] = map;
    }
    XtFree(m);
    e = XmTextFieldGetString(tf_extra);
    strncpy(extra_copy, e, sizeof(extra_copy) - 1);
    XtFree(e);
    for (tok = strtok(extra_copy, " \t"); tok && argc < 46; tok = strtok(NULL, " \t"))
        argv[argc++] = tok;
    argv[argc] = NULL;
    if ((child = fork()) == 0) {
        char lp[1200];
        struct stat st;
        time_t t = time(NULL);
        int lf, k;

        dup2(from[1], 1);
        sprintf(lp, "%s/amixq3.log", home);
        lf = open(lp, O_WRONLY | O_CREAT | O_APPEND | (stat(lp, &st) == 0 && st.st_size > 512 * 1024 ? O_TRUNC : 0),
                  0644);
        if (lf >= 0) {
            dup2(lf, 2);
            close(lf);
            fprintf(stderr, "\n=== %s", ctime(&t));
            for (k = 0; k < argc; k++)
                fprintf(stderr, "%s%s", k ? " " : "", argv[k]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        close(from[0]);
        close(from[1]);
        execv(C1Q3, argv);
        _exit(127);
    }
    close(from[1]);
    if (child < 0) {
        close(from[0]);
        say("Could not start c1q3.");
        return;
    }
    from_c1 = from[0];
    in_id = XtAppAddInput(app, from_c1, (XtPointer)XtInputReadMask, from_c1q3, NULL);
    XtSetSensitive(b_start, False);
    XtSetSensitive(b_stop, True);
    say("Loading the game into core 1 ...");
}

static void stop(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cd; (void)cb;
    if (child > 0)
        kill(child, SIGTERM);           /* c1q3 quits the game, which writes its config back */
    say("Stopping ...");
}

static void quit(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cd; (void)cb;
    setup_save();
    if (child > 0) {
        kill(child, SIGTERM);
        stopped();
    }
    exit(0);
}

static void size_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cb;
    size_i = (int)(long)cd;
}

int main(int argc, char **argv)
{
    Widget form, pulldown, row, sep, l, last;
    Arg a[2];
    XmString items[NMAPS];
    int i;

    signal(SIGPIPE, SIG_IGN);
    sprintf(home, "%.1000s/.q3a", getenv("HOME") ? getenv("HOME") : ".");
    mkdir(home, 0755);
    top = XtVaAppInitialize(&app, "Amixq3", NULL, 0, &argc, argv, NULL, XmNtitle, "Quake III", NULL);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top, NULL);

    pulldown = XmCreatePulldownMenu(form, "sizes", NULL, 0);
    for (i = 0; i < NSIZES; i++) {
        size_btn[i] = XtVaCreateManagedWidget(sizes[i], xmPushButtonWidgetClass, pulldown, NULL);
        XtAddCallback(size_btn[i], XmNactivateCallback, size_cb, (XtPointer)(long)i);
    }
    XtSetArg(a[0], XmNsubMenuId, pulldown);
    XtSetArg(a[1], XmNlabelString, XmStringCreateLocalized("Picture"));
    size_menu = XmCreateOptionMenu(form, "size", a, 2);
    XtVaSetValues(size_menu, XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 8,
                  XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
    XtManageChild(size_menu);

    l = XtVaCreateManagedWidget("Start in", xmLabelWidgetClass, form,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, size_menu, XmNtopOffset, 14,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 14, NULL);
    for (i = 0; i < NMAPS; i++)
        items[i] = XmStringCreateLocalized((char *)maps[i]);
    cb_map = XtVaCreateManagedWidget("map", xmComboBoxWidgetClass, form,
        XmNcomboBoxType, XmDROP_DOWN_COMBO_BOX, XmNitems, items, XmNitemCount, NMAPS,
        XmNvisibleItemCount, NMAPS, XmNcolumns, 24,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, size_menu, XmNtopOffset, 8,
        XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, l, XmNleftOffset, 6,
        XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 8, NULL);
    for (i = 0; i < NMAPS; i++)
        XmStringFree(items[i]);
    XmTextFieldSetString(XtNameToWidget(cb_map, "*Text"), "Main menu");

    l = XtVaCreateManagedWidget("Options", xmLabelWidgetClass, form,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, cb_map, XmNtopOffset, 14,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 14, NULL);
    tf_extra = XtVaCreateManagedWidget("extra", xmTextFieldWidgetClass, form, XmNcolumns, 40,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, cb_map, XmNtopOffset, 8,
        XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, l, XmNleftOffset, 6,
        XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 8, NULL);

    sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, form,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, tf_extra, XmNtopOffset, 8,
        XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    row = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, sep, XmNtopOffset, 6,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
    b_start = XtVaCreateManagedWidget("Start", xmPushButtonWidgetClass, row, NULL);
    b_stop = XtVaCreateManagedWidget("Stop", xmPushButtonWidgetClass, row, XmNsensitive, False, NULL);
    last = XtVaCreateManagedWidget("Quit", xmPushButtonWidgetClass, row, NULL);
    XtAddCallback(b_start, XmNactivateCallback, start, NULL);
    XtAddCallback(b_stop, XmNactivateCallback, stop, NULL);
    XtAddCallback(last, XmNactivateCallback, quit, NULL);
    status = XtVaCreateManagedWidget("Choose the picture and a map, then Start.  F10 quits the game.",
        xmLabelWidgetClass, form,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, row, XmNtopOffset, 6,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, XmNbottomAttachment, XmATTACH_FORM,
        XmNbottomOffset, 8, XmNalignment, XmALIGNMENT_BEGINNING, NULL);

    setup_load();
    XtRealizeWidget(top);
    XtAppMainLoop(app);
    return 0;
}
