/*
 * glut.c -- a GLUT subset for TinyGL on AMIX, over TinyGL's own GLX.
 * (2026-10-26)  See include/GL/glut.h for what is there.  One window; the
 * size is fixed by the WM hints unless a reshape callback is registered
 * (TinyGL reallocates its frame buffer on glViewport).
 *
 * glutInit takes -geometry WxH+X+Y and -display NAME and removes them from
 * argv, as GLUT does.  ESC is passed to the keyboard callback like any key;
 * closing the window from the window manager exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glut.h>

extern int gettimeofday();

static Display *dpy;
static Window win;
static GLXContext cx;
static Atom wmDelete;
static char *displayName;
static int winX = -1, winY = -1, winW = 300, winH = 300, geomFlags;
static int redisplay = 1;
static struct timeval startTime;

static void (*displayCB)(void);
static void (*idleCB)(void);
static void (*keyboardCB)(unsigned char, int, int);
static void (*specialCB)(int, int, int);
static void (*reshapeCB)(int, int);

#define NTIMERS 8
static struct timer {
    int used;
    struct timeval due;
    void (*func)(int);
    int value;
} timers[NTIMERS];

static void fatal(const char *msg)
{
    fprintf(stderr, "glut: %s\n", msg);
    exit(1);
}

static long msSince(const struct timeval *t)
{
    struct timeval now;
    gettimeofday(&now, 0);
    return (now.tv_sec - t->tv_sec) * 1000L + (now.tv_usec - t->tv_usec) / 1000L;
}

void glutInit(int *argc, char **argv)
{
    int i, j;

    gettimeofday(&startTime, 0);
    for (i = 1; i < *argc; ) {
        int used = 0;
        if (strcmp(argv[i], "-geometry") == 0 && i + 1 < *argc) {
            int x, y;
            unsigned int w, h;
            geomFlags = XParseGeometry(argv[i + 1], &x, &y, &w, &h);
            if (geomFlags & WidthValue)  winW = (int) w;
            if (geomFlags & HeightValue) winH = (int) h;
            if (geomFlags & XValue)      winX = x;
            if (geomFlags & YValue)      winY = y;
            used = 2;
        } else if (strcmp(argv[i], "-display") == 0 && i + 1 < *argc) {
            displayName = argv[i + 1];
            used = 2;
        }
        if (used) {
            for (j = i; j + used <= *argc; j++)
                argv[j] = argv[j + used];
            *argc -= used;
        } else
            i++;
    }
}

void glutInitDisplayMode(unsigned int mode)
{
    (void) mode;
}

void glutInitWindowSize(int width, int height)
{
    if (!(geomFlags & WidthValue))  winW = width;
    if (!(geomFlags & HeightValue)) winH = height;
}

void glutInitWindowPosition(int x, int y)
{
    if (!(geomFlags & XValue)) winX = x;
    if (!(geomFlags & YValue)) winY = y;
}

int glutCreateWindow(const char *title)
{
    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None };
    XSetWindowAttributes swa;
    XSizeHints hints;
    XVisualInfo *vi;
    XEvent ev;

    dpy = XOpenDisplay(displayName);
    if (!dpy)
        fatal("cannot open display");
    vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (!vi)
        fatal("no visual");
    cx = glXCreateContext(dpy, vi, 0, GL_TRUE);
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
    swa.border_pixel = 0;
    swa.background_pixel = BlackPixel(dpy, vi->screen);
    swa.event_mask = KeyPressMask | StructureNotifyMask | ExposureMask;
    win = XCreateWindow(dpy, RootWindow(dpy, vi->screen),
                        winX < 0 ? 0 : winX, winY < 0 ? 0 : winY, winW, winH, 0,
                        vi->depth, InputOutput, vi->visual,
                        CWBorderPixel | CWColormap | CWBackPixel | CWEventMask, &swa);
    hints.flags = PSize;
    if (winX >= 0 || winY >= 0)
        hints.flags |= USPosition;
    hints.x = winX < 0 ? 0 : winX;
    hints.y = winY < 0 ? 0 : winY;
    hints.width = winW;
    hints.height = winH;
    XSetStandardProperties(dpy, win, title, title, None, 0, 0, &hints);
    wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);
    XMapWindow(dpy, win);
    do
        XNextEvent(dpy, &ev);
    while (ev.type != MapNotify);
    glXMakeCurrent(dpy, win, cx);
    glViewport(0, 0, winW, winH);
    return 1;
}

void glutDisplayFunc(void (*func)(void))  { displayCB = func; }
void glutIdleFunc(void (*func)(void))     { idleCB = func; }
void glutKeyboardFunc(void (*func)(unsigned char, int, int)) { keyboardCB = func; }
void glutSpecialFunc(void (*func)(int, int, int)) { specialCB = func; }
void glutReshapeFunc(void (*func)(int, int)) { reshapeCB = func; }
void glutPostRedisplay(void)              { redisplay = 1; }
void glutSwapBuffers(void)                { glXSwapBuffers(dpy, win); }

void glutTimerFunc(unsigned int msecs, void (*func)(int), int value)
{
    int i;
    for (i = 0; i < NTIMERS; i++)
        if (!timers[i].used) {
            gettimeofday(&timers[i].due, 0);
            timers[i].due.tv_sec += msecs / 1000;
            timers[i].due.tv_usec += (msecs % 1000) * 1000L;
            if (timers[i].due.tv_usec >= 1000000L) {
                timers[i].due.tv_sec++;
                timers[i].due.tv_usec -= 1000000L;
            }
            timers[i].func = func;
            timers[i].value = value;
            timers[i].used = 1;
            return;
        }
    fprintf(stderr, "glut: too many timers\n");
}

int glutGet(int what)
{
    int x, y;
    Window child;
    XWindowAttributes wa;

    switch (what) {
    case GLUT_ELAPSED_TIME:
        return (int) msSince(&startTime);
    case GLUT_WINDOW_X:
    case GLUT_WINDOW_Y:
        XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &x, &y, &child);
        return what == GLUT_WINDOW_X ? x : y;
    case GLUT_WINDOW_WIDTH:
        return winW;
    case GLUT_WINDOW_HEIGHT:
        return winH;
    case GLUT_SCREEN_WIDTH:
    case GLUT_SCREEN_HEIGHT:
        XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
        return what == GLUT_SCREEN_WIDTH ? wa.width : wa.height;
    }
    return 0;
}

void glutPositionWindow(int x, int y)
{
    XMoveWindow(dpy, win, x, y);
}

void glutSetWindowTitle(const char *title)
{
    XStoreName(dpy, win, title);
}

static int special(KeySym k)
{
    switch (k) {
    case XK_F1: return GLUT_KEY_F1;   case XK_F2: return GLUT_KEY_F2;
    case XK_F3: return GLUT_KEY_F3;   case XK_F4: return GLUT_KEY_F4;
    case XK_F5: return GLUT_KEY_F5;   case XK_F6: return GLUT_KEY_F6;
    case XK_F7: return GLUT_KEY_F7;   case XK_F8: return GLUT_KEY_F8;
    case XK_F9: return GLUT_KEY_F9;   case XK_F10: return GLUT_KEY_F10;
    case XK_Left: return GLUT_KEY_LEFT;     case XK_Up: return GLUT_KEY_UP;
    case XK_Right: return GLUT_KEY_RIGHT;   case XK_Down: return GLUT_KEY_DOWN;
    case XK_Prior: return GLUT_KEY_PAGE_UP; case XK_Next: return GLUT_KEY_PAGE_DOWN;
    case XK_Home: return GLUT_KEY_HOME;     case XK_End: return GLUT_KEY_END;
    case XK_Insert: return GLUT_KEY_INSERT;
    }
    return 0;
}

static void event(XEvent *ev)
{
    char buf[8];
    KeySym k;
    int n, s;

    switch (ev->type) {
    case Expose:
        redisplay = 1;
        break;
    case ConfigureNotify:
        if (ev->xconfigure.width != winW || ev->xconfigure.height != winH) {
            winW = ev->xconfigure.width;
            winH = ev->xconfigure.height;
            if (reshapeCB)
                reshapeCB(winW, winH);
            else
                glViewport(0, 0, winW, winH);
            redisplay = 1;
        }
        break;
    case KeyPress:
        n = XLookupString(&ev->xkey, buf, sizeof buf, &k, 0);
        if (n > 0 && keyboardCB)
            keyboardCB((unsigned char) buf[0], ev->xkey.x, ev->xkey.y);
        else if (n == 0 && specialCB && (s = special(k)) != 0)
            specialCB(s, ev->xkey.x, ev->xkey.y);
        break;
    case ClientMessage:
        if ((Atom) ev->xclient.data.l[0] == wmDelete)
            exit(0);
        break;
    }
}

static void runTimers(void)
{
    struct timeval now;
    int i;

    gettimeofday(&now, 0);
    for (i = 0; i < NTIMERS; i++)
        if (timers[i].used &&
            (now.tv_sec > timers[i].due.tv_sec ||
             (now.tv_sec == timers[i].due.tv_sec && now.tv_usec >= timers[i].due.tv_usec))) {
            timers[i].used = 0;
            timers[i].func(timers[i].value);
        }
}

static int timersPending(void)
{
    int i;
    for (i = 0; i < NTIMERS; i++)
        if (timers[i].used)
            return 1;
    return 0;
}

void glutMainLoop(void)
{
    XEvent ev;

    if (reshapeCB)
        reshapeCB(winW, winH);
    for (;;) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            event(&ev);
        }
        runTimers();
        if (redisplay && displayCB) {
            redisplay = 0;
            displayCB();
        } else if (idleCB)
            idleCB();
        else if (!redisplay && !timersPending()) {
            /* nothing to do: sleep until the next event */
            XPeekEvent(dpy, &ev);
        }
    }
}
