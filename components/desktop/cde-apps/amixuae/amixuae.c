/*
 * amixuae -- Amiga: the CDE front end of the Amiga emulator on the
 * Z3660's ARM core 1 (emulators/uae4arm).  Choose the model, the Kickstart,
 * floppies DF0-DF3 and a hard file, then Start: a window opens where the
 * Amiga's picture is (the RTG overlay, drawn by core 1, never through X),
 * and the window's keys, mouse and joystick go to the Amiga.
 *
 * The board is c1uae's (root, /dev/zz9000); amixuae runs it and talks to
 * it through a pipe, one command a line (see c1uae.c).
 *
 * In the Amiga's window:
 *   click                  the Amiga gets the mouse (the X pointer hides; the
 *                          Amiga draws its own)
 *   Left Amiga + Right Amiga, or both mouse buttons held 2 s
 *                          gives keyboard and mouse back to CDE (the
 *                          kernel takes Alt/Amiga + Help, Alt + F-keys and
 *                          Alt + Esc before X: never use those)
 *   Keys as joystick       cursor keys = port 2 stick, Right Alt (also as
 *                          AltGr) or keypad 0 fire, Right Amiga second button
 *
 * Files on the command line (CDE passes them for Open and for a drop on
 * the panel control): .adf/.adz/.dms go into DF0, DF1 ..., .hdf into the
 * hard file, a Kickstart (.rom, .bin, kick*.a500 ...: by name, else by its
 * size and first bytes) into the Kickstart field.  With a disk among them
 * the Amiga starts at once.
 *
 * The AmixUAE folder, ~/Documents/AmixUAE (made at start, as FS-UAE's):
 *   Kickstarts/  Floppies/  Hard Drives/  Configurations/
 * The "..." dialogs open in the field's folder; a file in its folder is
 * shown and stored by its name alone.  Configurations/NAME.amixuae holds a
 * whole setup (the key=value lines below): pick it in Configuration, Save
 * under the name typed there, Delete.  A .amixuae file given to amixuae
 * (Open in the File Manager, a drop on the panel) loads and starts it.
 *
 * The last setup in ~/.amixuae (one key=value a line, config= its name).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/keysym.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/FileSB.h>
#include <Xm/DrawingA.h>
#include <Xm/Separator.h>
#include <Xm/MessageB.h>
#include <Xm/ComboBox.h>
#include <Xm/List.h>

#define C1UAE       "/opt/amix/bin/c1uae"
#define PIC_W       640
#define PIC_H       512

/* libretro's key codes (SDL 1.2's), as the core reads them */
#define RK_BACKSPACE 8
#define RK_TAB      9
#define RK_RETURN   13
#define RK_ESCAPE   27
#define RK_DELETE   127
#define RK_KP0      256
#define RK_KP_PERIOD 266
#define RK_KP_DIVIDE 267
#define RK_KP_MULTIPLY 268
#define RK_KP_MINUS 269
#define RK_KP_PLUS  270
#define RK_KP_ENTER 271
#define RK_UP       273
#define RK_DOWN     274
#define RK_RIGHT    275
#define RK_LEFT     276
#define RK_INSERT   277     /* the core's Help */
#define RK_HOME     278     /* the keypad's ( */
#define RK_END      279     /* the keypad's ) */
#define RK_F1       282
#define RK_CAPSLOCK 301
#define RK_RSHIFT   303
#define RK_LSHIFT   304
#define RK_LCTRL    306
#define RK_RALT     307
#define RK_LALT     308
#define RK_RMETA    309     /* Right Amiga */
#define RK_LMETA    310     /* Left Amiga */

/* RETRO_DEVICE_ID_JOYPAD_* bits */
#define JOY_B       (1 << 0)
#define JOY_A       (1 << 8)
#define JOY_UP      (1 << 4)
#define JOY_DOWN    (1 << 5)
#define JOY_LEFT    (1 << 6)
#define JOY_RIGHT   (1 << 7)

static XtAppContext app;
static Widget top, model_menu, model_btn[3], tf_kick, tf_df[4], tf_hdf, tg_ro, tg_joy, tg_scale,
              b_start, b_stop, b_reset, status, emu_shell, emu_area, cb_conf;
static const char *models[3] = { "A500", "A600", "A1200" };
static int model = 0;
static pid_t child = -1;
static FILE *to_c1;
static int from_c1 = -1;
static XtInputId in_id;
static int grabbed, joy_bits, held_ramiga, held_lamiga, held_ralt, scale = 1;
static Cursor blank;

/* --- the AmixUAE folder: ~/Documents/AmixUAE --- */
enum { D_KICK, D_FLOPPY, D_HDF, D_CONF, D_N };
static const char *dir_names[D_N] = { "Kickstarts", "Floppies", "Hard Drives", "Configurations" };
static char base_dir[1024];

static const char *folder(int d)
{
    static char p[D_N][1100];

    sprintf(p[d], "%s/%s", base_dir, dir_names[d]);
    return p[d];
}

static void folders_make(void)
{
    const char *h = getenv("HOME");
    char p[1100];
    FILE *f;
    int d;

    sprintf(p, "%.900s/Documents", h ? h : "/tmp");
    mkdir(p, 0755);                     /* existing ones just fail */
    sprintf(base_dir, "%.900s/Documents/AmixUAE", h ? h : "/tmp");
    mkdir(base_dir, 0755);
    for (d = 0; d < D_N; d++)
        mkdir(folder(d), 0755);
    sprintf(p, "%s/README.txt", base_dir);
    if (access(p, F_OK) != 0 && (f = fopen(p, "w")) != NULL) {
        fprintf(f, "AmixUAE -- the Amiga on the Z3660's ARM core 1 (Amiga in the AMIX panel).\n\n"
                   "Kickstarts/      your own Kickstart ROMs (.rom, also .bin in EPROM byte order)\n"
                   "Floppies/        floppy images: .adf .adz .dms\n"
                   "Hard Drives/     hard files: .hdf\n"
                   "Configurations/  saved setups, NAME.amixuae (Save in the Amiga window;\n"
                   "                 opening one in the File Manager starts it)\n\n"
                   "A file in its folder is named by its name alone in a configuration.\n");
        fclose(f);
    }
}

/* a field's value as shown and stored: a file in its folder by its name */
static const char *short_name(const char *path, int d)
{
    const char *dir = folder(d);
    size_t n = strlen(dir);

    if (!strncmp(path, dir, n) && path[n] == '/' && path[n + 1] && !strchr(path + n + 1, '/'))
        return path + n + 1;
    return path;
}

/* and back: a name alone is in the field's folder */
static char *full_name(const char *v, int d)
{
    char *p = XtMalloc((Cardinal)(strlen(v) + 1100));

    if (*v && !strchr(v, '/'))
        sprintf(p, "%s/%s", folder(d), v);
    else
        strcpy(p, v);
    return p;
}

static void set_field(Widget t, const char *v, int d)
{
    XmTextFieldSetString(t, (char *)short_name(v, d));
}

/* --- settings: a setup in key=value lines --- */
static char *settings_path(void)
{
    static char p[1024];
    const char *h = getenv("HOME");

    sprintf(p, "%.1000s/.amixuae", h ? h : "/tmp");
    return p;
}

static char conf_name[256];

static int cfg_load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1200], *v;
    int i;

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || !(v = strchr(line, '=')))
            continue;
        *v++ = 0;
        if (!strcmp(line, "model"))
            for (i = 0; i < 3; i++)
                if (!strcmp(v, models[i]))
                    model = i;
        if (!strcmp(line, "kick")) set_field(tf_kick, v, D_KICK);
        if (!strcmp(line, "hdf")) set_field(tf_hdf, v, D_HDF);
        if (!strncmp(line, "df", 2) && line[2] >= '0' && line[2] <= '3' && !line[3])
            set_field(tf_df[line[2] - '0'], v, D_FLOPPY);
        if (!strcmp(line, "ro")) XmToggleButtonSetState(tg_ro, atoi(v), False);
        if (!strcmp(line, "joy")) XmToggleButtonSetState(tg_joy, atoi(v), False);
        if (!strcmp(line, "scale")) XmToggleButtonSetState(tg_scale, atoi(v) == 2, False);
        if (!strcmp(line, "config")) {
            strncpy(conf_name, v, sizeof(conf_name) - 1);
            conf_name[sizeof(conf_name) - 1] = 0;
        }
    }
    fclose(f);
    XtVaSetValues(model_menu, XmNmenuHistory, model_btn[model], NULL);
    return 0;
}

static int cfg_save(const char *path, const char *name)
{
    FILE *f = fopen(path, "w");
    char *s;
    int i;

    if (!f)
        return -1;
    fprintf(f, "# AmixUAE configuration (amixuae; names alone are in ~/Documents/AmixUAE/...)\n");
    if (name)
        fprintf(f, "config=%s\n", name);
    fprintf(f, "model=%s\n", models[model]);
    fprintf(f, "kick=%s\n", s = XmTextFieldGetString(tf_kick)); XtFree(s);
    for (i = 0; i < 4; i++) {
        fprintf(f, "df%d=%s\n", i, s = XmTextFieldGetString(tf_df[i]));
        XtFree(s);
    }
    fprintf(f, "hdf=%s\n", s = XmTextFieldGetString(tf_hdf)); XtFree(s);
    fprintf(f, "ro=%d\njoy=%d\nscale=%d\n", XmToggleButtonGetState(tg_ro), XmToggleButtonGetState(tg_joy),
            XmToggleButtonGetState(tg_scale) ? 2 : 1);
    return fclose(f) == 0 ? 0 : -1;
}

static void settings_load(void)
{
    cfg_load(settings_path());
}

static void settings_save(void)
{
    cfg_save(settings_path(), conf_name);
}

static void say(const char *s)
{
    XmString x = XmStringCreateLocalized((char *)s);

    XtVaSetValues(status, XmNlabelString, x, NULL);
    XmStringFree(x);
}

/* --- to c1uae --- */
static void send(const char *fmt, int a, int b)
{
    if (!to_c1)
        return;
    fprintf(to_c1, fmt, a, b);
    fputc('\n', to_c1);
    fflush(to_c1);
}

/* --- keys --- */
static int retrok(KeySym k)
{
    if (k >= XK_a && k <= XK_z) return (int)k;
    if (k >= XK_A && k <= XK_Z) return (int)(k - XK_A + XK_a);
    if (k >= XK_0 && k <= XK_9) return (int)k;
    if (k >= XK_F1 && k <= XK_F10) return RK_F1 + (int)(k - XK_F1);
    if (k >= XK_KP_0 && k <= XK_KP_9) return RK_KP0 + (int)(k - XK_KP_0);
    switch (k) {
    case XK_space: case XK_minus: case XK_equal: case XK_bracketleft: case XK_bracketright:
    case XK_semicolon: case XK_apostrophe: case XK_comma: case XK_period: case XK_slash:
    case XK_backslash: case XK_grave: return (int)k;
    case XK_BackSpace: return RK_BACKSPACE;
    case XK_Tab: return RK_TAB;
    case XK_Return: return RK_RETURN;
    case XK_Escape: return RK_ESCAPE;
    case XK_Delete: return RK_DELETE;
    case XK_Up: return RK_UP;
    case XK_Down: return RK_DOWN;
    case XK_Left: return RK_LEFT;
    case XK_Right: return RK_RIGHT;
    case XK_Help: case XK_Insert: return RK_INSERT;
    case XK_KP_Decimal: return RK_KP_PERIOD;
    case XK_KP_Divide: return RK_KP_DIVIDE;
    case XK_KP_Multiply: return RK_KP_MULTIPLY;
    case XK_KP_Subtract: return RK_KP_MINUS;
    case XK_KP_Add: return RK_KP_PLUS;
    case XK_KP_Enter: return RK_KP_ENTER;
    case XK_Caps_Lock: return RK_CAPSLOCK;
    case XK_Shift_L: return RK_LSHIFT;
    case XK_Shift_R: return RK_RSHIFT;
    case XK_Control_L: case XK_Control_R: return RK_LCTRL;     /* the Amiga has one Ctrl */
    case XK_Alt_L: return RK_LALT;
    case XK_Alt_R: case XK_Mode_switch: case XK_ISO_Level3_Shift: return RK_RALT;
    case XK_Meta_L: case XK_Super_L: return RK_LMETA;          /* Left Amiga (check xev on the box) */
    case XK_Meta_R: case XK_Super_R: case XK_Multi_key: return RK_RMETA;  /* Right Amiga */
    }
    return -1;
}

/* Right Alt: with a German (or any AltGr) keymap X calls it Mode_switch or
   ISO_Level3_Shift, not Alt_R */
static int is_ralt(KeySym k)
{
    return k == XK_Alt_R || k == XK_Mode_switch || k == XK_ISO_Level3_Shift;
}

static void ungrab(void)
{
    if (!grabbed)
        return;
    XUngrabPointer(XtDisplay(emu_area), CurrentTime);
    XUngrabKeyboard(XtDisplay(emu_area), CurrentTime);
    XUndefineCursor(XtDisplay(emu_area), XtWindow(emu_area));
    grabbed = 0;
    send("buttons %d", 0, 0);
    send("key %d %d", 0, RK_LMETA);         /* the release keys are not left held */
    send("key %d %d", 0, RK_RMETA);
    if (joy_bits) {
        joy_bits = 0;
        send("joy %d %d", 0, 0);
    }
    held_lamiga = held_ramiga = 0;
    say("Running. Click into the Amiga to give it the mouse.");
}

static void grab(void)
{
    Display *d = XtDisplay(emu_area);
    Window w = XtWindow(emu_area);

    if (grabbed)
        return;
    XDefineCursor(d, w, blank);
    if (XGrabPointer(d, w, True, PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, w, blank, CurrentTime) != GrabSuccess)
        return;
    XGrabKeyboard(d, w, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    XWarpPointer(d, None, w, 0, 0, 0, 0, PIC_W * scale / 2, PIC_H * scale / 2);
    grabbed = 1;
    say("The Amiga has mouse and keyboard: Left Amiga + Right Amiga (or both mouse buttons for 2 s) gives them back.");
}

static void key(int down, KeySym k)
{
    int code;

    if (k == XK_Meta_R || k == XK_Super_R || k == XK_Multi_key)
        held_ramiga = down;
    if (is_ralt(k))
        held_ralt = down;
    if (k == XK_Meta_L || k == XK_Super_L)
        held_lamiga = down;
    if (down && held_lamiga && held_ramiga) {
        ungrab();
        return;
    }
    if (XmToggleButtonGetState(tg_joy)) {
        int bit = 0;

        switch (k) {
        case XK_Up: bit = JOY_UP; break;
        case XK_Down: bit = JOY_DOWN; break;
        case XK_Left: bit = JOY_LEFT; break;
        case XK_Right: bit = JOY_RIGHT; break;
        case XK_Alt_R: case XK_Mode_switch: case XK_ISO_Level3_Shift:
        case XK_KP_0: case XK_KP_Insert: bit = JOY_B; break;   /* fire: Right Alt, or keypad 0 */
        case XK_Meta_R: case XK_Super_R: case XK_Multi_key: bit = JOY_A; break;
        }
        if (bit) {
            joy_bits = down ? joy_bits | bit : joy_bits & ~bit;
            send("joy %d %d", 0, joy_bits);    /* libretro port 0: the Amiga's joystick port 2 */
            return;
        }
    }
    if ((code = retrok(k)) >= 0)
        send("key %d %d", down, code);
}

/* both mouse buttons held 2 s: back to CDE, whatever the keymap calls keys */
static XtIntervalId both_id;
static int buttons;

static void both_held(XtPointer cd, XtIntervalId *id)
{
    (void)cd; (void)id;
    both_id = 0;
    if (grabbed && (buttons & 3) == 3) {
        buttons = 0;
        ungrab();
    }
}

static void area_event(Widget w, XtPointer cd, XEvent *ev, Boolean *cont)
{

    (void)w; (void)cd; (void)cont;
    switch (ev->type) {
    case KeyPress:
    case KeyRelease:
        key(ev->type == KeyPress, XLookupKeysym(&ev->xkey, 0));
        break;
    case ButtonPress:
    case ButtonRelease:
        if (!grabbed) {
            if (ev->type == ButtonPress && child > 0)
                grab();
            break;
        }
        if (ev->xbutton.button >= 1 && ev->xbutton.button <= 3) {
            const int bit = ev->xbutton.button == 1 ? 1 : ev->xbutton.button == 3 ? 2 : 4;

            buttons = ev->type == ButtonPress ? buttons | bit : buttons & ~bit;
            send("buttons %d", buttons, 0);
            if ((buttons & 3) == 3 && !both_id)
                both_id = XtAppAddTimeOut(app, 2000, both_held, NULL);
            else if ((buttons & 3) != 3 && both_id) {
                XtRemoveTimeOut(both_id);
                both_id = 0;
            }
        }
        break;
    case MotionNotify:
        if (grabbed) {
            const int cx = PIC_W * scale / 2, cy = PIC_H * scale / 2;
            const int dx = ev->xmotion.x - cx, dy = ev->xmotion.y - cy;

            if (dx || dy) {
                send("mouse %d %d", dx, dy);
                XWarpPointer(XtDisplay(emu_area), None, XtWindow(emu_area), 0, 0, 0, 0, cx, cy);
            }
        }
        break;
    }
}

/* where the window is, to the overlay */
static void place(void)
{
    Window child_w;
    int x, y;

    if (!emu_area || !XtIsRealized(emu_area))
        return;
    XTranslateCoordinates(XtDisplay(emu_area), XtWindow(emu_area), DefaultRootWindow(XtDisplay(emu_area)),
                          0, 0, &x, &y, &child_w);
    send("place %d %d", x, y);
}

static void shell_event(Widget w, XtPointer cd, XEvent *ev, Boolean *cont)
{
    (void)w; (void)cd; (void)cont;
    if (ev->type == ConfigureNotify)
        place();
    else if (ev->type == UnmapNotify) {
        ungrab();
        send("hide", 0, 0);
    } else if (ev->type == MapNotify) {
        send("show", 0, 0);
        place();
    }
}

/* --- c1uae's status lines --- */
static void stopped(void)
{
    int st;

    if (in_id) {
        XtRemoveInput(in_id);
        in_id = 0;
    }
    if (to_c1) {
        fclose(to_c1);
        to_c1 = NULL;
    }
    if (from_c1 >= 0) {
        close(from_c1);
        from_c1 = -1;
    }
    if (child > 0 && waitpid(child, &st, 0) == child && WIFEXITED(st)) {
        if (WEXITSTATUS(st) == 3)
            say("The Amiga is already running (another window has it).");
        else if (WEXITSTATUS(st) == 127)
            say("c1uae is not installed (emulators/uae4arm).");
        else if (WEXITSTATUS(st) != 0)
            say("The Amiga could not start: see c1uae -v in a terminal.");
    }
    child = -1;
    ungrab();
    if (emu_shell)
        XtPopdown(emu_shell);
    XtSetSensitive(b_start, True);
    XtSetSensitive(b_stop, False);
    XtSetSensitive(b_reset, False);
}

static void from_c1uae(XtPointer cd, int *src, XtInputId *id)
{
    static char buf[512];
    static int n;
    int r;

    (void)cd; (void)id;
    r = (int)read(*src, buf + n, sizeof(buf) - 1 - n);
    if (r <= 0) {
        say("The Amiga stopped.");
        stopped();                      /* says why, if it did not end well */
        return;
    }
    n += r;
    buf[n] = 0;
    for (;;) {
        char *nl = strchr(buf, '\n');
        unsigned long frames, us, under, dirty, st;

        if (!nl)
            break;
        *nl = 0;
        if (sscanf(buf, "status %lu %lu %lu %lu %lu", &frames, &us, &under, &dirty, &st) == 5 && !grabbed) {
            char s[200];

            sprintf(s, "Running: frame %lu, %lu.%lu ms a frame (20 = full speed), sound gaps %lu%s",
                    frames, us / 1000, us / 100 % 10, under, dirty ? ", saving disk" : "");
            say(s);
        }
        n -= (int)(nl + 1 - buf);
        memmove(buf, nl + 1, (size_t)n + 1);
    }
    if (n >= (int)sizeof(buf) - 1)
        n = 0;
}

/* --- Start, Stop, Reset --- */
static void start(Widget w, XtPointer cd, XtPointer cb)
{
    char *argv[32], *kick, *df[4], *hdf;
    int to[2], from[2], argc = 0, i, x, y;
    Window cw;

    (void)w; (void)cd; (void)cb;
    if (child > 0)
        return;
    {
        char *v;

        kick = full_name(v = XmTextFieldGetString(tf_kick), D_KICK); XtFree(v);
        for (i = 0; i < 4; i++) {
            df[i] = full_name(v = XmTextFieldGetString(tf_df[i]), D_FLOPPY);
            XtFree(v);
        }
        hdf = full_name(v = XmTextFieldGetString(tf_hdf), D_HDF); XtFree(v);
    }
    if (!*kick || (!*df[0] && !*hdf)) {
        say("A Kickstart and a disk in DF0 (or a hard file) are needed.");
        return;
    }
    settings_save();
    scale = XmToggleButtonGetState(tg_scale) ? 2 : 1;
    XtVaSetValues(emu_area, XmNwidth, PIC_W * scale, XmNheight, PIC_H * scale, NULL);
    XtPopup(emu_shell, XtGrabNone);
    XSync(XtDisplay(emu_area), False);
    XTranslateCoordinates(XtDisplay(emu_area), XtWindow(emu_area), DefaultRootWindow(XtDisplay(emu_area)),
                          0, 0, &x, &y, &cw);
    if (pipe(to) < 0 || pipe(from) < 0) {
        say("No pipe.");
        return;
    }
    argv[argc++] = C1UAE;
    argv[argc++] = "-model";
    argv[argc++] = (char *)models[model];
    argv[argc++] = "-kick";
    argv[argc++] = kick;
    for (i = 0; i < 4; i++)
        if (*df[i]) {
            static char opt[4][6];

            sprintf(opt[i], "-df%d", i);
            argv[argc++] = opt[i];
            argv[argc++] = df[i];
        }
    if (*hdf) {
        argv[argc++] = "-hdf";
        argv[argc++] = hdf;
    }
    if (XmToggleButtonGetState(tg_ro))
        argv[argc++] = "-ro";
    if (scale == 2) {
        argv[argc++] = "-scale";
        argv[argc++] = "2";
    }
    {
        static char xs[16], ys[16];

        sprintf(xs, "%d", x);
        sprintf(ys, "%d", y);
        argv[argc++] = "-at";
        argv[argc++] = xs;
        argv[argc++] = ys;
    }
    argv[argc] = NULL;
    if ((child = fork()) == 0) {
        dup2(to[0], 0);
        dup2(from[1], 1);
        close(to[0]); close(to[1]); close(from[0]); close(from[1]);
        execv(C1UAE, argv);
        _exit(127);
    }
    close(to[0]);
    close(from[1]);
    if (child < 0) {
        close(to[1]);
        close(from[0]);
        say("Could not start c1uae.");
        return;
    }
    to_c1 = fdopen(to[1], "w");
    from_c1 = from[0];
    in_id = XtAppAddInput(app, from_c1, (XtPointer)XtInputReadMask, from_c1uae, NULL);
    XtSetSensitive(b_start, False);
    XtSetSensitive(b_stop, True);
    XtSetSensitive(b_reset, True);
    say("Loading the Kickstart and the disks into core 1 ...");
}

static void stop(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cd; (void)cb;
    send("stop", 0, 0);         /* c1uae saves the disks, then ends: from_c1uae sees it */
    say("Stopping, saving changed disks ...");
}

static void reset(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cd; (void)cb;
    send("reset", 0, 0);
}

static void quit(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cd; (void)cb;
    if (child > 0) {
        send("stop", 0, 0);
        stopped();
    }
    settings_save();
    exit(0);
}

/* --- the windows' icon: our own XPM (1 char a pixel, as mkicons.py writes
   it), read without libXpm --- */
#define ICON_FILE   "/etc/dt/appconfig/icons/C/AmixUAE.l.pm"

static void window_icon(Widget shell)
{
    Display *d = XtDisplay(top);
    Window root = RootWindowOfScreen(XtScreen(top));
    Colormap cm = DefaultColormapOfScreen(XtScreen(top));
    FILE *f = fopen(ICON_FILE, "r");
    char line[256], key[64];
    unsigned long pix[64];
    int w = 0, h = 0, nc = 0, i = 0, y = 0;
    Pixmap p = 0, mask = 0;
    GC gc = 0, mgc = 0;

    if (!f)
        return;
    memset(key, 0, sizeof(key));
    while (fgets(line, sizeof(line), f)) {
        char *q = strchr(line, '"'), *e;

        if (!q || !(e = strchr(q + 1, '"')))
            continue;
        *e = 0;
        q++;
        if (!w) {
            if (sscanf(q, "%d %d %d", &w, &h, &nc) != 3 || w > 64 || h > 64 || nc > 63)
                break;
            p = XCreatePixmap(d, root, (unsigned)w, (unsigned)h, (unsigned)DefaultDepthOfScreen(XtScreen(top)));
            mask = XCreatePixmap(d, root, (unsigned)w, (unsigned)h, 1);
            gc = XCreateGC(d, p, 0, NULL);
            mgc = XCreateGC(d, mask, 0, NULL);
        } else if (i < nc) {            /* "X c #RRGGBB" or "X c None" */
            XColor c, exact;

            key[i] = q[0];
            if (strstr(q + 1, "None"))
                pix[i] = (unsigned long)-1;
            else if (XAllocNamedColor(d, cm, strrchr(q, ' ') + 1, &c, &exact))
                pix[i] = c.pixel;
            else
                pix[i] = BlackPixelOfScreen(XtScreen(top));
            i++;
        } else if (y < h) {
            int x;

            for (x = 0; x < w && q[x]; x++) {
                const char *k = strchr(key, q[x]);
                const unsigned long v = k ? pix[k - key] : (unsigned long)-1;

                XSetForeground(d, mgc, v == (unsigned long)-1 ? 0 : 1);
                XDrawPoint(d, mask, mgc, x, y);
                if (v != (unsigned long)-1) {
                    XSetForeground(d, gc, v);
                    XDrawPoint(d, p, gc, x, y);
                }
            }
            y++;
        }
    }
    fclose(f);
    if (gc)
        XFreeGC(d, gc);
    if (mgc)
        XFreeGC(d, mgc);
    if (p && y == h)
        XtVaSetValues(shell, XmNiconPixmap, p, XmNiconMask, mask, NULL);
}

/* --- files given on the command line --- */
enum { F_NONE, F_FLOPPY, F_HDF, F_KICK, F_CONF };

static int ext_is(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    int i;

    if (!dot)
        return 0;
    for (i = 0; ext[i] && dot[1 + i]; i++)
        if ((dot[1 + i] | 0x20) != ext[i])
            return 0;
    return !ext[i] && !dot[1 + i];
}

static int kind(const char *path)
{
    const char *base = strrchr(path, '/');
    unsigned char head[4];
    FILE *f;
    long size;

    base = base ? base + 1 : path;
    if (ext_is(path, "adf") || ext_is(path, "adz") || ext_is(path, "dms"))
        return F_FLOPPY;
    if (ext_is(path, "hdf"))
        return F_HDF;
    if (ext_is(path, "amixuae"))
        return F_CONF;
    if (ext_is(path, "rom") || !strncmp(base, "kick", 4))
        return F_KICK;
    if (!(f = fopen(path, "rb")))
        return F_NONE;
    fseek(f, 0L, SEEK_END);
    size = ftell(f);
    rewind(f);
    head[0] = head[1] = 0;
    fread(head, 1, 4, f);
    fclose(f);
    if (size == 901120)
        return F_FLOPPY;
    if ((size == 262144 || size == 524288) &&
        ((head[0] == 0x11 && (head[1] == 0x11 || head[1] == 0x14)) || (head[0] == 0x14 && head[1] == 0x11)))
        return F_KICK;                      /* also EPROM byte order (.bin) */
    return F_NONE;
}

static int conf_open(const char *path);

static void autostart(XtPointer cd, XtIntervalId *id)
{
    (void)cd; (void)id;
    start(NULL, NULL, NULL);
}

/* returns 1 when a disk came, so the Amiga starts */
static int take_files(int argc, char **argv)
{
    int i, df = 0, disk = 0;

    for (i = 1; i < argc; i++) {
        char *full = argv[i];

        switch (kind(full)) {
        case F_FLOPPY:
            if (df == 0) {                  /* new disks replace the last session's */
                int k;

                for (k = 0; k < 4; k++)
                    XmTextFieldSetString(tf_df[k], "");
                XmTextFieldSetString(tf_hdf, "");
            }
            if (df < 4)
                set_field(tf_df[df++], full, D_FLOPPY);
            disk = 1;
            break;
        case F_HDF:
            if (!disk) {
                int k;

                for (k = 0; k < 4; k++)
                    XmTextFieldSetString(tf_df[k], "");
            }
            set_field(tf_hdf, full, D_HDF);
            disk = 1;
            break;
        case F_KICK:
            set_field(tf_kick, full, D_KICK);
            break;
        case F_CONF:                        /* a whole setup: load it and start */
            if (conf_open(full) == 0)
                disk = 1;
            break;
        default: {
            char m[300];

            sprintf(m, "Not an Amiga disk or Kickstart: %.250s", full);
            say(m);
        }
        }
    }
    return disk;
}

/* --- file choice: in the field's folder (or where its file is) --- */
static int field_dir(Widget t)
{
    XtPointer d;

    XtVaGetValues(t, XmNuserData, &d, NULL);
    return (int)(long)d;
}

static void file_ok(Widget w, XtPointer cd, XtPointer cb)
{
    XmFileSelectionBoxCallbackStruct *c = (XmFileSelectionBoxCallbackStruct *)cb;
    char *name;

    if (XmStringGetLtoR(c->value, XmFONTLIST_DEFAULT_TAG, &name)) {
        set_field((Widget)cd, name, field_dir((Widget)cd));
        XtFree(name);
    }
    XtUnmanageChild(w);
}

static void file_cancel(Widget w, XtPointer cd, XtPointer cb)
{
    (void)cd; (void)cb;
    XtUnmanageChild(w);
}

static void browse(Widget w, XtPointer cd, XtPointer cb)
{
    Widget t = (Widget)cd, d;
    char *v = XmTextFieldGetString(t), *full = full_name(v, field_dir(t)), *slash;
    XmString dir;
    Arg a[1];

    (void)w; (void)cb;
    if (*full && (slash = strrchr(full, '/')) != NULL && slash != full) {
        *slash = 0;                     /* where the field's file is */
        dir = XmStringCreateLocalized(full);
    } else
        dir = XmStringCreateLocalized((char *)folder(field_dir(t)));
    XtSetArg(a[0], XmNdirectory, dir);
    d = XmCreateFileSelectionDialog(top, "choose", a, 1);
    XmStringFree(dir);
    XtFree(v);
    XtFree(full);
    XtAddCallback(d, XmNokCallback, file_ok, cd);
    XtAddCallback(d, XmNcancelCallback, file_cancel, NULL);
    XtManageChild(d);
}

/* --- configurations: Configurations/NAME.amixuae --- */
static char *conf_path(const char *name)
{
    static char p[1500];

    sprintf(p, "%s/%.250s.amixuae", folder(D_CONF), name);
    return p;
}

static Widget conf_text(void)
{
    Widget t;

    XtVaGetValues(cb_conf, XmNtextField, &t, NULL);
    return t;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* the list from the folder; the shown name stays */
static void conf_list(void)
{
    Widget list;
    DIR *d = opendir(folder(D_CONF));
    struct dirent *e;
    char *names[256];
    int n = 0, i;

    XtVaGetValues(cb_conf, XmNlist, &list, NULL);
    XmListDeleteAllItems(list);
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && n < 256) {
        size_t l = strlen(e->d_name);

        if (l > 8 && e->d_name[0] != '.' && !strcmp(e->d_name + l - 8, ".amixuae")) {
            names[n] = XtMalloc((Cardinal)l);
            memcpy(names[n], e->d_name, l - 8);
            names[n][l - 8] = 0;
            n++;
        }
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), by_name);
    for (i = 0; i < n; i++) {
        XmString x = XmStringCreateLocalized(names[i]);

        XmListAddItemUnselected(list, x, 0);
        XmStringFree(x);
        XtFree(names[i]);
    }
    XtVaSetValues(list, XmNvisibleItemCount, n < 1 ? 1 : n > 10 ? 10 : n, NULL);
}

/* the name in the field, usable as a file name; "" when not */
static char *conf_typed(void)
{
    static char n[256];
    char *v = XmTextFieldGetString(conf_text()), *p = v, *q;

    while (*p == ' ')
        p++;
    strncpy(n, p, sizeof(n) - 1);
    n[sizeof(n) - 1] = 0;
    XtFree(v);
    for (q = n + strlen(n); q > n && q[-1] == ' '; )
        *--q = 0;
    if (n[0] == '.' || strchr(n, '/'))
        n[0] = 0;
    return n;
}

static void conf_show(const char *name)
{
    strncpy(conf_name, name, sizeof(conf_name) - 1);
    conf_name[sizeof(conf_name) - 1] = 0;
    XmTextFieldSetString(conf_text(), conf_name);
}

/* a .amixuae file (from the list, Open in the File Manager, a drop) */
static int conf_open(const char *path)
{
    const char *base = strrchr(path, '/');
    char name[256], m[400];
    int k;

    for (k = 0; k < 4; k++)             /* a setup is whole: no disks left over */
        XmTextFieldSetString(tf_df[k], "");
    XmTextFieldSetString(tf_hdf, "");
    if (cfg_load(path) < 0) {
        sprintf(m, "Cannot read %.300s", path);
        say(m);
        return -1;
    }
    strncpy(name, base ? base + 1 : path, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    if (strlen(name) > 8 && !strcmp(name + strlen(name) - 8, ".amixuae"))
        name[strlen(name) - 8] = 0;
    conf_show(name);
    sprintf(m, "Configuration %.250s.", name);
    say(m);
    return 0;
}

static void conf_pick(Widget w, XtPointer cd, XtPointer cb)
{
    XmComboBoxCallbackStruct *c = (XmComboBoxCallbackStruct *)cb;
    char *name;

    (void)w; (void)cd;
    if (c->reason != XmCR_SELECT || !c->event || c->event->type == KeyPress)
        return;                         /* typing a new name loads nothing */
    if (XmStringGetLtoR(c->item_or_text, XmFONTLIST_DEFAULT_TAG, &name)) {
        if (*name && access(conf_path(name), R_OK) == 0)
            conf_open(conf_path(name));
        XtFree(name);
    }
}

static void conf_save(Widget w, XtPointer cd, XtPointer cb)
{
    const char *name = conf_typed();
    char m[400];

    (void)w; (void)cd; (void)cb;
    if (!*name) {
        say("Type a name for the configuration first (no / in it).");
        return;
    }
    if (cfg_save(conf_path(name), name) < 0) {
        sprintf(m, "Cannot write %.300s", conf_path(name));
        say(m);
        return;
    }
    conf_show(name);
    conf_list();
    settings_save();
    sprintf(m, "Saved configuration %.250s.", name);
    say(m);
}

static void conf_delete_ok(Widget w, XtPointer cd, XtPointer cb)
{
    char *name = (char *)cd, m[400];

    (void)cb;
    if (unlink(conf_path(name)) == 0) {
        sprintf(m, "Deleted configuration %.250s.", name);
        conf_show("");
        conf_list();
        settings_save();
    } else
        sprintf(m, "Cannot delete %.300s", conf_path(name));
    say(m);
    XtFree(name);
    XtDestroyWidget(XtParent(w));
}

static void conf_delete_no(Widget w, XtPointer cd, XtPointer cb)
{
    (void)cb;
    XtFree((char *)cd);
    XtDestroyWidget(XtParent(w));
}

static void conf_delete(Widget w, XtPointer cd, XtPointer cb)
{
    const char *name = conf_typed();
    char m[400];
    XmString x;
    Widget d;
    Arg a[1];

    (void)w; (void)cd; (void)cb;
    if (!*name || access(conf_path(name), F_OK) != 0) {
        say("Choose a saved configuration to delete.");
        return;
    }
    sprintf(m, "Delete the configuration %.250s?\n(The disks and ROMs it names stay.)", name);
    x = XmStringCreateLocalized(m);
    XtSetArg(a[0], XmNmessageString, x);
    d = XmCreateQuestionDialog(top, "delete", a, 1);
    XmStringFree(x);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtAddCallback(d, XmNokCallback, conf_delete_ok, XtNewString(name));
    XtAddCallback(d, XmNcancelCallback, conf_delete_no, XtNewString(name));
    XtVaSetValues(XtParent(d), XmNtitle, "Amiga: delete configuration", NULL);
    XtManageChild(d);
}

static void model_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w; (void)cb;
    model = (int)(long)cd;
}

/* a label, a text field and a "..." button in one row of the form */
static Widget file_row(Widget form, Widget above, const char *label, int dir)
{
    Widget l, t, b;

    l = XtVaCreateManagedWidget(label, xmLabelWidgetClass, form,
        XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM, XmNtopWidget, above, XmNtopOffset, 6,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, XmNwidth, 80, XmNalignment, XmALIGNMENT_END, NULL);
    b = XtVaCreateManagedWidget("...", xmPushButtonWidgetClass, form,
        XmNtopAttachment, XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, l,
        XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 8, NULL);
    t = XtVaCreateManagedWidget("file", xmTextFieldWidgetClass, form,
        XmNtopAttachment, XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, l, XmNtopOffset, -4,
        XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, l, XmNleftOffset, 6,
        XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, b, XmNrightOffset, 4, XmNcolumns, 40,
        XmNuserData, (XtPointer)(long)dir, NULL);
    XtAddCallback(b, XmNactivateCallback, browse, (XtPointer)t);
    return t;
}

int main(int argc, char **argv)
{
    Widget form, pulldown, last, row, sep;
    Arg a[4];
    XColor black;
    Pixmap p;
    char none = 0;
    int i;

    signal(SIGPIPE, SIG_IGN);
    top = XtVaAppInitialize(&app, "Amixuae", NULL, 0, &argc, argv, NULL, XmNtitle, "Amiga", NULL);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top, NULL);
    folders_make();

    /* Configuration: the saved setups, Save, Delete */
    {
        Widget l, bs, bd;

        l = XtVaCreateManagedWidget("Configuration", xmLabelWidgetClass, form,
            XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 12,
            XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
        bd = XtVaCreateManagedWidget("Delete", xmPushButtonWidgetClass, form,
            XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 8,
            XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 8, NULL);
        bs = XtVaCreateManagedWidget("Save", xmPushButtonWidgetClass, form,
            XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 8,
            XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, bd, XmNrightOffset, 4, NULL);
        cb_conf = XtVaCreateManagedWidget("config", xmComboBoxWidgetClass, form,
            XmNcomboBoxType, XmDROP_DOWN_COMBO_BOX,
            XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 6,
            XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, l, XmNleftOffset, 6,
            XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, bs, XmNrightOffset, 6, NULL);
        XtAddCallback(cb_conf, XmNselectionCallback, conf_pick, NULL);
        XtAddCallback(bs, XmNactivateCallback, conf_save, NULL);
        XtAddCallback(bd, XmNactivateCallback, conf_delete, NULL);
    }

    pulldown = XmCreatePulldownMenu(form, "models", NULL, 0);
    for (i = 0; i < 3; i++) {
        model_btn[i] = XtVaCreateManagedWidget(models[i], xmPushButtonWidgetClass, pulldown, NULL);
        XtAddCallback(model_btn[i], XmNactivateCallback, model_cb, (XtPointer)(long)i);
    }
    XtSetArg(a[0], XmNsubMenuId, pulldown);
    XtSetArg(a[1], XmNlabelString, XmStringCreateLocalized("Model"));
    model_menu = XmCreateOptionMenu(form, "model", a, 2);
    XtVaSetValues(model_menu, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, cb_conf, XmNtopOffset, 6,
                  XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 40, NULL);
    XtManageChild(model_menu);

    tf_kick = file_row(form, model_menu, "Kickstart", D_KICK);
    last = tf_kick;
    for (i = 0; i < 4; i++) {
        char l[8];

        sprintf(l, "DF%d", i);
        tf_df[i] = file_row(form, last, l, D_FLOPPY);
        last = tf_df[i];
    }
    tf_hdf = file_row(form, last, "Hard file", D_HDF);

    row = XtVaCreateManagedWidget("options", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, tf_hdf, XmNtopOffset, 8,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
    tg_ro = XtVaCreateManagedWidget("Write-protect disks", xmToggleButtonWidgetClass, row, NULL);
    tg_joy = XtVaCreateManagedWidget("Keys as joystick", xmToggleButtonWidgetClass, row, NULL);
    tg_scale = XtVaCreateManagedWidget("Double size", xmToggleButtonWidgetClass, row, NULL);

    sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, form,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, row, XmNtopOffset, 6,
        XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    row = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, sep, XmNtopOffset, 6,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, NULL);
    b_start = XtVaCreateManagedWidget("Start", xmPushButtonWidgetClass, row, NULL);
    b_stop = XtVaCreateManagedWidget("Stop", xmPushButtonWidgetClass, row, XmNsensitive, False, NULL);
    b_reset = XtVaCreateManagedWidget("Reset", xmPushButtonWidgetClass, row, XmNsensitive, False, NULL);
    last = XtVaCreateManagedWidget("Quit", xmPushButtonWidgetClass, row, NULL);
    XtAddCallback(b_start, XmNactivateCallback, start, NULL);
    XtAddCallback(b_stop, XmNactivateCallback, stop, NULL);
    XtAddCallback(b_reset, XmNactivateCallback, reset, NULL);
    XtAddCallback(last, XmNactivateCallback, quit, NULL);
    status = XtVaCreateManagedWidget("Choose a Kickstart and a disk, then Start.", xmLabelWidgetClass, form,
        XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, row, XmNtopOffset, 6,
        XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 8, XmNbottomAttachment, XmATTACH_FORM,
        XmNbottomOffset, 8, XmNalignment, XmALIGNMENT_BEGINNING, NULL);

    /* the Amiga's window: black, the overlay lies over it */
    emu_shell = XtVaCreatePopupShell("Amiga", topLevelShellWidgetClass, top, XmNtitle, "Amiga",
                                     XmNallowShellResize, True, NULL);
    emu_area = XtVaCreateManagedWidget("screen", xmDrawingAreaWidgetClass, emu_shell,
        XmNwidth, PIC_W, XmNheight, PIC_H, XmNbackground, BlackPixelOfScreen(XtScreen(top)),
        XmNtraversalOn, True, NULL);
    XtAddEventHandler(emu_area, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask, False, area_event, NULL);
    XtAddEventHandler(emu_shell, StructureNotifyMask, False, shell_event, NULL);

    settings_load();
    conf_list();
    XmTextFieldSetString(conf_text(), conf_name);
    XtRealizeWidget(top);
    window_icon(top);
    window_icon(emu_shell);
    if (take_files(argc, argv))
        XtAppAddTimeOut(app, 500, autostart, NULL);
    p = XCreateBitmapFromData(XtDisplay(top), XtWindow(top), &none, 1, 1);
    black.red = black.green = black.blue = 0;
    blank = XCreatePixmapCursor(XtDisplay(top), p, p, &black, &black, 0, 0);
    XtAppMainLoop(app);
    return 0;
}
