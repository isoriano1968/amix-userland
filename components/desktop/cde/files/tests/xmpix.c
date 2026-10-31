/* xmpix.c -- can Motif load CDE's backdrops?  dtwm loads them with
 * XmGetPixmap(); its "Unable to get image" goes to the session log, which
 * CDE sends to /dev/null. */
#include <stdio.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

int main(int argc, char **argv)
{
	XtAppContext app;
	Widget top = XtAppInitialize(&app, "Xmpix", NULL, 0, &argc, argv, NULL, NULL, 0);
	Screen *s = XtScreen(top);
	Pixel fg = BlackPixelOfScreen(s), bg = WhitePixelOfScreen(s);
	int i;

	for (i = 1; i < argc; i++) {
		Pixmap p = XmGetPixmap(s, argv[i], fg, bg);
		unsigned int w = 0, h = 0, bw, d = 0;
		int x, y;
		Window r;
		if (p != XmUNSPECIFIED_PIXMAP)
			XGetGeometry(XtDisplay(top), p, &r, &x, &y, &w, &h, &bw, &d);
		printf("%-40s %s %ux%u depth %u\n", argv[i],
		    p == XmUNSPECIFIED_PIXMAP ? "FAILED" : "ok", w, h, d);
	}
	return 0;
}
