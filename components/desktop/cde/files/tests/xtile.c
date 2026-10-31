/* xtile.c -- does the Xzz9000 server paint a window background tile?
 * dtwm draws backdrops as the background pixmap of a full-screen window;
 * on the A3000 every backdrop shows only a colour.  Window 1: background
 * pixmap from XmGetPixmap (as dtwm).  Window 2: the same pixmap copied with
 * XCopyArea (no tiling).  Both stay up for SECONDS.
 *   xtile FILE [SECONDS] */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

int main(int argc, char **argv)
{
	XtAppContext app;
	Widget top = XtAppInitialize(&app, "Xtile", NULL, 0, &argc, argv, NULL, NULL, 0);
	Display *dpy = XtDisplay(top);
	Screen *s = XtScreen(top);
	Window root = RootWindowOfScreen(s), w1, w2;
	Pixmap p;
	unsigned int pw, ph, bw, d;
	int x, y, secs = argc > 2 ? atoi(argv[2]) : 20;
	Window r;
	XSetWindowAttributes a;
	GC gc;

	if (argc < 2) return 2;
	p = XmGetPixmap(s, argv[1], BlackPixelOfScreen(s), WhitePixelOfScreen(s));
	if (p == XmUNSPECIFIED_PIXMAP) { printf("XmGetPixmap failed\n"); return 1; }
	XGetGeometry(dpy, p, &r, &x, &y, &pw, &ph, &bw, &d);
	printf("pixmap %ux%u depth %u\n", pw, ph, d);
	a.background_pixmap = p;
	a.override_redirect = True;
	w1 = XCreateWindow(dpy, root, 100, 100, 600, 400, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWBackPixmap | CWOverrideRedirect, &a);
	a.background_pixel = WhitePixelOfScreen(s);
	w2 = XCreateWindow(dpy, root, 760, 100, 600, 400, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWBackPixel | CWOverrideRedirect, &a);
	XMapRaised(dpy, w1);
	XMapRaised(dpy, w2);
	XSync(dpy, False);
	sleep(1);
	gc = XCreateGC(dpy, w2, 0, NULL);
	XCopyArea(dpy, p, w2, gc, 0, 0, pw < 600 ? pw : 600, ph < 400 ? ph : 400, 0, 0);
	XSync(dpy, False);
	printf("windows up for %d s\n", secs);
	fflush(stdout);
	sleep(secs);
	return 0;
}
