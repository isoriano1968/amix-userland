/* xpix.c -- which off-screen pixmap path does the Xzz9000 server lose?
 * Four 200x150 windows side by side, each should show a black/red pattern:
 *  1 XPutImage straight into the window
 *  2 XFillRectangle into a pixmap, XCopyArea to the window
 *  3 XPutImage into a pixmap, XCopyArea to the window
 *  4 the pixmap from 3 as the window's background tile
 *   xpix [SECONDS] */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(NULL);
	int scr, secs = argc > 1 ? atoi(argv[1]) : 30, i, x, y;
	Window root, w[4];
	XSetWindowAttributes a;
	Pixmap p2, p3;
	GC gc;
	XImage *im;
	Visual *vis;
	unsigned int depth;
	XColor red, ex;

	if (!dpy) return 1;
	scr = DefaultScreen(dpy); root = RootWindow(dpy, scr);
	vis = DefaultVisual(dpy, scr); depth = DefaultDepth(dpy, scr);
	XAllocNamedColor(dpy, DefaultColormap(dpy, scr), "red", &red, &ex);
	a.override_redirect = True;
	a.background_pixel = WhitePixel(dpy, scr);
	for (i = 0; i < 4; i++)
		w[i] = XCreateWindow(dpy, root, 100 + i * 220, 600, 200, 150, 0, CopyFromParent,
		    InputOutput, CopyFromParent, CWBackPixel | CWOverrideRedirect, &a);
	gc = XCreateGC(dpy, root, 0, NULL);

	/* an image: red/black checker of 16x16 squares */
	im = XCreateImage(dpy, vis, depth, ZPixmap, 0, malloc(64 * 64 * 4), 64, 64, 32, 0);
	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++)
			XPutPixel(im, x, y, ((x / 16 + y / 16) & 1) ? red.pixel : BlackPixel(dpy, scr));

	p2 = XCreatePixmap(dpy, root, 64, 64, depth);
	XSetForeground(dpy, gc, WhitePixel(dpy, scr));
	XFillRectangle(dpy, p2, gc, 0, 0, 64, 64);
	XSetForeground(dpy, gc, red.pixel);
	XFillRectangle(dpy, p2, gc, 0, 0, 32, 32);
	XSetForeground(dpy, gc, BlackPixel(dpy, scr));
	XFillRectangle(dpy, p2, gc, 32, 32, 32, 32);

	p3 = XCreatePixmap(dpy, root, 64, 64, depth);
	XPutImage(dpy, p3, gc, im, 0, 0, 0, 0, 64, 64);

	XSetWindowBackgroundPixmap(dpy, w[3], p3);
	for (i = 0; i < 4; i++) XMapRaised(dpy, w[i]);
	XSync(dpy, False);
	sleep(1);
	XPutImage(dpy, w[0], gc, im, 0, 0, 10, 10, 64, 64);
	XCopyArea(dpy, p2, w[1], gc, 0, 0, 64, 64, 10, 10);
	XCopyArea(dpy, p3, w[2], gc, 0, 0, 64, 64, 10, 10);
	XSync(dpy, False);
	printf("depth %u, windows up for %d s\n", depth, secs);
	fflush(stdout);
	sleep(secs);
	return 0;
}
