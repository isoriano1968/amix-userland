/* xmcolor.c -- Motif's colour calculation on the A3000: which foreground do
 * white, grey and dark backgrounds get?  dtsession's colour server uses the
 * same XmGetColorCalculation() and gave dtpad white text on white. */
#include <stdio.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

static void one(XmColorProc calc, unsigned r, unsigned g, unsigned b)
{
	XColor bg, fg, sel, ts, bs;
	bg.red = r; bg.green = g; bg.blue = b;
	calc(&bg, &fg, &sel, &ts, &bs);
	printf("bg %04x %04x %04x -> fg %04x %04x %04x  ts %04x %04x %04x\n",
	    r, g, b, fg.red, fg.green, fg.blue, ts.red, ts.green, ts.blue);
}

int main(int argc, char **argv)
{
	XtAppContext app;
	Widget top = XtAppInitialize(&app, "Xmcolor", NULL, 0, &argc, argv, NULL, NULL, 0);
	XmColorProc calc = XmGetColorCalculation();
	volatile float f = 0.30f;
	volatile int i = 65535;
	printf("float test: (int)(0.30f*65535)=%d\n", (int)(f * (float)i));
	one(calc, 0xffff, 0xffff, 0xffff);
	one(calc, 0xc630, 0xb2ca, 0xad6a);
	one(calc, 0x8000, 0x8000, 0x8000);
	one(calc, 0x2000, 0x2000, 0x4000);
	return 0;
}
