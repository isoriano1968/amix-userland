/*
 * libXtInherit.a: _XtInherit in the program.  A class record in a program that
 * is not PIC would hold the PLT stub as _XtInherit, and ld.so.1 passes over
 * undefined symbols, so libXt, comparing with its own function, would miss
 * every inherited method.  Defined here, all bind to this one.  xt.pc names it
 * before -lXt; a program that does not need it does not pull it in.
 */
#include <X11/Intrinsic.h>

void _XtInherit(void)
{
	XtErrorMsg("invalidProcedure", "inheritanceProc", "XtToolkitError",
		"Unresolved inheritance operation", (String *)NULL, (Cardinal *)NULL);
}
