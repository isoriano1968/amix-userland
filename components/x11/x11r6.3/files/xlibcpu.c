/* xlibcpu.c -- client-side Xlib work, timed: what a rebuilt libX11 changes.
 * (2026-10-26)  Built twice with gcc14-cross-amix, once against AMIX's own
 * static R6.3 libraries (gcc 2.7, -O, 68020) and once against lib060/
 * (gcc 14, -O2 -m68060); the program itself is the same code both times.
 * Only the first test talks to the server, and only once.
 *
 *   xlibcpu [resource-file]     default /usr/dt/app-defaults/C/Dtterm
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void say(const char *what, double ms, int n)
{
    printf("%-34s %8.1f ms  (%d)\n", what, ms, n);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *file = argc > 1 ? argv[1] : "/usr/dt/app-defaults/C/Dtterm";
    static const char *names[] = { "LightSteelBlue", "#4f4f9f9f8f8f", "rgb:ff/80/00",
                                   "DarkSlateGray", "antique white", "#c0c0c0" };
    Display *dpy;
    XrmDatabase db;
    XFontStruct *fs;
    XColor c;
    Colormap cmap;
    char *type, buf[64];
    XrmValue v;
    double t;
    int i, n = 0;

    XrmInitialize();
    dpy = XOpenDisplay(0);
    if (!dpy) { printf("no display\n"); return 1; }
    cmap = DefaultColormap(dpy, DefaultScreen(dpy));

    t = now();
    for (i = 0; i < 30; i++) {
        db = XrmGetFileDatabase(file);
        if (!db) { printf("cannot read %s\n", file); return 1; }
        if (i < 29)
            XrmDestroyDatabase(db);
    }
    say("XrmGetFileDatabase x30", now() - t, 30);

    t = now();
    for (i = 0; i < 20000; i++)
        n += XrmGetResource(db, i & 1 ? "dtterm.vt100.foreground" : "dtterm.menuBar.file.labelString",
                            i & 1 ? "Dtterm.Vt100.Foreground" : "Dtterm.MenuBar.File.LabelString",
                            &type, &v);
    say("XrmGetResource x20000", now() - t, n);

    t = now();
    for (i = 0; i < 20000; i++) {
        sprintf(buf, "*widget%d.resource%d: value %d", i % 97, i % 31, i);
        XrmPutLineResource(&db, buf);
    }
    say("XrmPutLineResource x20000", now() - t, 20000);

    t = now();
    for (n = 0, i = 0; i < 20000; i++) {
        sprintf(buf, "ATOM_%d", i % 500);
        n += XrmStringToQuark(buf) != 0;
    }
    say("XrmStringToQuark x20000", now() - t, n);

    t = now();
    for (n = 0, i = 0; i < 3000; i++)
        n += XParseColor(dpy, cmap, names[i % 6], &c) != 0;
    say("XParseColor x3000 (rgb db lookups)", now() - t, n);

    fs = XLoadQueryFont(dpy, "fixed");
    if (fs) {
        int dir, asc, desc;
        XCharStruct cs;
        static const char txt[] = "The quick brown fox jumps over the lazy dog 0123456789";
        t = now();
        for (n = 0, i = 0; i < 20000; i++) {
            XTextExtents(fs, txt, sizeof(txt) - 1, &dir, &asc, &desc, &cs);
            n += XTextWidth(fs, txt, sizeof(txt) - 1) > 0;
        }
        say("XTextExtents+XTextWidth x20000", now() - t, n);
    }
    XCloseDisplay(dpy);
    return 0;
}
