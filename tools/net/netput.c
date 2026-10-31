/* netput.c -- send one file over TCP from AMIX: connect to HOST:PORT, write
 * FILE and close.  Pair: netrecv.py on the PC.  The mirror of netget.c.
 *   netput HOST PORT FILE */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

/* AMIX (SVR4.0) declares no socket prototypes */
extern int socket(int, int, int);
extern int connect(int, const struct sockaddr *, int);

int main(int argc, char **argv)
{
	struct sockaddr_in sa;
	static char buf[65536];
	long total = 0, n;
	int s, fd;
	struct timeval t0, t1;

	if (argc != 4) {
		fprintf(stderr, "usage: netput host port file\n");
		return 2;
	}
	s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(atoi(argv[2]));
	sa.sin_addr.s_addr = inet_addr(argv[1]);
	if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
		perror("connect");
		return 1;
	}
	fd = open(argv[3], O_RDONLY);
	if (fd < 0) {
		perror(argv[3]);
		return 1;
	}
	gettimeofday(&t0, NULL);
	while ((n = read(fd, buf, sizeof buf)) > 0) {
		if (write(s, buf, n) != n) {
			perror("write");
			return 1;
		}
		total += n;
	}
	gettimeofday(&t1, NULL);
	close(fd);
	close(s);
	{
		double t = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;
		printf("netput: %ld bytes in %.1f s (%.0f KB/s)\n", total, t, t > 0 ? total / 1024.0 / t : 0.0);
	}
	return n < 0;
}
