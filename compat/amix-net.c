/*
 * amix-net.c -- connect() for FFmpeg on AMIX: FFmpeg connects non-blocking
 * and polls, but AMIX's poll() says writable while the handshake is still
 * running, and the first send then fails with ENOTCONN.  So the descriptor
 * is blocking for the connect.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

extern int connect(int, const struct sockaddr *, int);

int
amix_connect(int s, const struct sockaddr *a, int l)
{
	const int fl = fcntl(s, F_GETFL, 0);
	int r;

	if (fl != -1)
		fcntl(s, F_SETFL, fl & ~(O_NDELAY | O_NONBLOCK));
	r = connect(s, (struct sockaddr *)a, l);
	if (fl != -1)
		fcntl(s, F_SETFL, fl);
	return r;
}
