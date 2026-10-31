/* xmtext.c -- does a plain Motif XmText draw its text on the Xzz9000 server?
 * dtpad's text area stays white.  Two widgets: an XmText (multi-line, as in
 * dtpad) and an XmTextField, both with a value set directly.
 *   xmtext [SECONDS] */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>

static int secs;
static void quit(XtPointer c, XtIntervalId *id) { exit(0); }

int main(int argc, char **argv)
{
	XtAppContext app;
	Widget top, rc, t, tf;
	Arg a[8];
	int n;

	top = XtAppInitialize(&app, "Xmtext", NULL, 0, &argc, argv, NULL, NULL, 0);
	secs = argc > 1 ? atoi(argv[1]) : 40;
	rc = XmCreateRowColumn(top, "rc", NULL, 0);
	n = 0;
	XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	XtSetArg(a[n], XmNrows, 5); n++;
	XtSetArg(a[n], XmNcolumns, 40); n++;
	XtSetArg(a[n], XmNvalue, "XmText line one\nline two: Hello AMIX\nline three"); n++;
	t = XmCreateText(rc, "text", a, n);
	n = 0;
	XtSetArg(a[n], XmNvalue, "XmTextField: Hello AMIX"); n++;
	tf = XmCreateTextField(rc, "field", a, n);
	XtManageChild(t);
	XtManageChild(tf);
	XtManageChild(rc);
	XtRealizeWidget(top);
	XtAppAddTimeOut(app, secs * 1000L, quit, NULL);
	XtAppMainLoop(app);
	return 0;
}
