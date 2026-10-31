/* pixset.c -- the pixel sets dtsession's colour server publishes (what
 * every Motif client's colour object receives), with their RGB values. */
#include <stdio.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/XmP.h>
#include <Xm/ColorObjP.h>

int main(int argc, char **argv)
{
	XtAppContext app;
	Widget top = XtAppInitialize(&app, "Pixset", NULL, 0, &argc, argv, NULL, NULL, 0);
	Display *d = XtDisplay(top);
	XmPixelSet ps[XmCO_NUM_COLORS];
	int colorUse, i;
	short a = -1, in = -1, p = -1, s = -1, t = -1;
	XColor c[2];

	if (!XmeGetPixelData(DefaultScreen(d), &colorUse, ps, &a, &in, &p, &s)) {
		printf("XmeGetPixelData failed (no colour server?)\n");
		return 1;
	}
	XmeGetColorObjData(XtScreen(top), &colorUse, ps, XmCO_NUM_COLORS, &a, &in, &p, &s, &t);
	printf("colorUse %d active %d inactive %d primary %d secondary %d text %d\n",
	    colorUse, a, in, p, s, t);
	for (i = 0; i < XmCO_NUM_COLORS; i++) {
		c[0].pixel = ps[i].bg; c[1].pixel = ps[i].fg;
		XQueryColors(d, DefaultColormap(d, DefaultScreen(d)), c, 2);
		printf("set %d: bg %04lx (%04x %04x %04x)  fg %04lx (%04x %04x %04x)\n", i,
		    ps[i].bg, c[0].red, c[0].green, c[0].blue,
		    ps[i].fg, c[1].red, c[1].green, c[1].blue);
	}
	return 0;
}
