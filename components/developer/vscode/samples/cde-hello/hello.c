/*
 * hello.c -- the smallest Motif/CDE program for AMIX: a window with
 * "Hello, world" and a Quit button.
 *
 *   make                    gcc 14 (m68k-elf-gcc), the default
 *   make TOOLCHAIN=gcc272   gcc 2.7.2.3 (m68k-cbm-sysv4-gcc)
 *
 * On the A3000: DISPLAY=:0 ./hello   (or just ./hello from a CDE dtterm)
 */
#include <stdlib.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>

static void quit(Widget w, XtPointer client, XtPointer call)
{
    exit(0);
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget top, form, label, button;
    XmString s;

    top = XtVaAppInitialize(&app, "Hello", NULL, 0, &argc, argv, NULL,
                            XmNtitle, "Hello AMIX", NULL);

    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top,
                                   XmNmarginWidth, 12, XmNmarginHeight, 12,
                                   NULL);

    s = XmStringCreateLocalized("Hello, world");
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, form,
                                    XmNlabelString, s,
                                    XmNtopAttachment, XmATTACH_FORM,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM,
                                    NULL);
    XmStringFree(s);

    s = XmStringCreateLocalized("Quit");
    button = XtVaCreateManagedWidget("quit", xmPushButtonWidgetClass, form,
                                     XmNlabelString, s,
                                     XmNtopAttachment, XmATTACH_WIDGET,
                                     XmNtopWidget, label,
                                     XmNtopOffset, 12,
                                     XmNbottomAttachment, XmATTACH_FORM,
                                     XmNleftAttachment, XmATTACH_POSITION,
                                     XmNleftPosition, 30,
                                     XmNrightAttachment, XmATTACH_POSITION,
                                     XmNrightPosition, 70,
                                     NULL);
    XmStringFree(s);
    XtAddCallback(button, XmNactivateCallback, quit, NULL);

    XtRealizeWidget(top);
    XtAppMainLoop(app);
    return 0;
}
