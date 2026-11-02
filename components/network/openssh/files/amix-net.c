/* amix-net.c -- AMIX differences: connect under way is EAGAIN, libgen dirname */
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#undef connect

int
amix_connect(int s, const struct sockaddr *a, int n)
{
	int r = connect(s, a, n);

	if (r < 0 && errno == EAGAIN)
		errno = EINPROGRESS;
	return r;
}

/* libgen's dirname("/.ssh") is "", not "/": sshd then cannot check root's
   home and refuses every key.  POSIX's, in place */
char *
amix_dirname(char *p)
{
	char *e;

	if (p == NULL || *p == '\0')
		return ".";
	e = p + strlen(p) - 1;
	while (e > p && *e == '/')
		e--;
	while (e > p && *e != '/')
		e--;
	if (*e != '/')
		return ".";
	while (e > p && *e == '/')
		e--;
	e[1] = '\0';
	return p;
}
