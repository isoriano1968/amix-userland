/* x11probe.c -- which part of opening an X display kills us?
 *
 * OpenTTD dies with SIGILL and pc=0 inside XOpenDisplay, reproducibly, with a
 * live server.  The frame walk names amix_gfx_open as the caller and the
 * disassembly shows the fault is at the call itself, so the fault is inside
 * Xlib -- but "inside Xlib" is thirty thousand lines.
 *
 * Same instrument as stdioprobe.c: write(1, ...) markers, which are syscalls,
 * cannot buffer, and do not depend on anything under test.  The last marker
 * printed names the step that died.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>

#define MARK(s) write(1, "[" s "]\n", (unsigned)strlen(s) + 3)

int main(void)
{
	Display *d;
	char *e;

	MARK("start");

	MARK("before getenv(DISPLAY)");
	e = getenv("DISPLAY");
	MARK("after getenv");
	if (e != NULL) { write(1, "DISPLAY=", 8); write(1, e, (unsigned)strlen(e)); write(1, "\n", 1); }
	else MARK("DISPLAY is NULL");

	MARK("before XOpenDisplay(NULL)");
	d = XOpenDisplay(NULL);
	MARK("after XOpenDisplay(NULL)");
	if (d == NULL) {
		MARK("returned NULL");
	} else {
		MARK("got a display");
		MARK("before DefaultScreen");
		{
			int s = DefaultScreen(d);
			int w = DisplayWidth(d, s), h = DisplayHeight(d, s);
			int dp = DefaultDepth(d, s);
			char buf[80];
			sprintf(buf, "screen %d: %dx%d depth %d\n", s, w, h, dp);
			write(1, buf, (unsigned)strlen(buf));
		}
		MARK("after DefaultScreen");
		MARK("before XCloseDisplay");
		XCloseDisplay(d);
		MARK("after XCloseDisplay");
	}

	MARK("done");
	return 0;
}
