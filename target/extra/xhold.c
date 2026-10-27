/*
 * xhold.c -- hold one X connection open until the server goes away.
 * The X11R6.3 Xzz9000 resets whenever its last client disconnects (it has no
 * -noreset): the font path goes back to the default and new connections are
 * refused while it resets.  Under startx, CDE's Xsession runs xset as its
 * first client, so that reset undid the font path and broke the next clients.
 * dtlogin holds a connection for this reason; xhold does it for startcde.
 * No window, no events: it only waits for the connection to close.
 * Usage (xinitrc): xhold; exec Xsession -- returns once connected.
 */
#include <X11/Xlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(void)
{
	Display *d = NULL;
	char buf[256];
	int i;

	/* startx starts the session as soon as the server process runs, before
	** it accepts connections: retry, so the session's first clients do not
	** have to */
	for (i = 0; i < 60 && !(d = XOpenDisplay(NULL)); i++)
		sleep(1);
	if (!d) {
		fprintf(stderr, "xhold: cannot open display\n");
		return 1;
	}
	/* connected: the parent returns, so "xhold; exec Xsession" starts the
	** session only now; the child holds the connection */
	if (fork() != 0)
		_exit(0);
	/* Xlib made the connection non-blocking: a read would return at once */
	i = fcntl(ConnectionNumber(d), F_GETFL, 0);
	fcntl(ConnectionNumber(d), F_SETFL, i & ~(O_NONBLOCK | O_NDELAY));
	while (read(ConnectionNumber(d), buf, sizeof buf) > 0)
		;		/* nothing is ever asked for, so nothing arrives */
	return 0;
}
