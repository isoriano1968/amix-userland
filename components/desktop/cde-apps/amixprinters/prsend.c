/*
 * prsend.c -- send one file to a network printer's raw port (9100 style)
 * and wait until the printer has taken it.  The lp interface scripts of
 * amixprinters call it:
 *
 *   prsend IP PORT FILE [OPTIONS]
 *
 * OPTIONS: a comma (or space) separated list, later entries win; the
 * interface passes the printer's defaults first, then the job's lp -o:
 *
 *   paper=a4|a3|a5|letter|legal   (or just a4, letter, ...)
 *   sides=one|two-long|two-short  (or simplex, duplex, tumble)
 *   color=color|mono              (or colour, bw, gray, grey)
 *
 * They go into the job: a PJL header (@PJL SET PAPER/DUPLEX/BINDING/
 * RENDERMODE, then ENTER LANGUAGE for PostScript and PDF), and for
 * PostScript also setpagedevice right after the first line, each key on its
 * own inside "stopped" so a key the printer lacks cannot fail the job.  The
 * file itself goes out unchanged.  Without options nothing is added.
 *
 * A blocking connect (SVR4.0's sockmod breaks non-blocking ones), the file
 * in 32 KB writes, then a half close and reading until the printer closes,
 * so lp does not report the job done while it is still on the wire.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UEL "\033%-12345X"

static const char *paper, *sides, *color;

struct paper { const char *name; int w, h; const char *pjl; };

static const struct paper papers[] = {
	{ "a4", 595, 842, "A4" }, { "a3", 842, 1191, "A3" }, { "a5", 420, 595, "A5" },
	{ "letter", 612, 792, "LETTER" }, { "legal", 612, 1008, "LEGAL" },
};
#define NPAPER (int)(sizeof papers / sizeof papers[0])

static void option(const char *o)
{
	int i;
	const char *v = strchr(o, '=') ? strchr(o, '=') + 1 : o;

	if (!*o)
		return;
	for (i = 0; i < NPAPER; i++)
		if (!strcmp(v, papers[i].name) && (v == o || !strncmp(o, "paper=", 6)))
			paper = papers[i].name;
	if (!strcmp(o, "sides=one") || !strcmp(o, "simplex") || !strcmp(o, "one-sided"))
		sides = "one";
	else if (!strcmp(o, "sides=two-long") || !strcmp(o, "duplex") || !strcmp(o, "two-sided"))
		sides = "two-long";
	else if (!strcmp(o, "sides=two-short") || !strcmp(o, "tumble"))
		sides = "two-short";
	else if (!strcmp(o, "color=color") || !strcmp(o, "color") || !strcmp(o, "colour"))
		color = "color";
	else if (!strcmp(o, "color=mono") || !strcmp(o, "mono") || !strcmp(o, "bw") ||
		 !strcmp(o, "gray") || !strcmp(o, "grey"))
		color = "mono";
}

static void options(const char *s)
{
	char o[64];
	int n;

	while (*s) {
		n = strcspn(s, ", \t");
		if (n > 0 && n < (int)sizeof o) {
			memcpy(o, s, n);
			o[n] = 0;
			option(o);
		}
		s += n;
		while (*s == ',' || *s == ' ' || *s == '\t')
			s++;
	}
}

static int s_;

static int put(const char *p, int n)
{
	while (n > 0) {
		int w = write(s_, p, n);

		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			fprintf(stderr, "prsend: write: %s\n", strerror(errno));
			return -1;
		}
		p += w;
		n -= w;
	}
	return 0;
}

static int puts_(const char *p)
{
	return put(p, strlen(p));
}

static const struct paper *find_paper(void)
{
	int i;

	for (i = 0; paper && i < NPAPER; i++)
		if (!strcmp(paper, papers[i].name))
			return &papers[i];
	return NULL;
}

static void on_alarm(int sig)
{
	(void)sig;
}

int main(int argc, char **argv)
{
	struct sockaddr_in a;
	char buf[32768], line[300];
	int fd, n, have, ps, pdf, wrap;

	if (argc != 4 && argc != 5) {
		fprintf(stderr, "usage: prsend IP PORT FILE [OPTIONS]\n");
		return 2;
	}
	if (argc == 5)
		options(argv[4]);
	if ((fd = open(argv[3], O_RDONLY)) < 0) {
		perror(argv[3]);
		return 1;
	}
	have = read(fd, buf, sizeof buf);
	if (have < 0)
		have = 0;
	ps = have >= 2 && buf[0] == '%' && buf[1] == '!';
	pdf = have >= 5 && !memcmp(buf, "%PDF-", 5);
	wrap = paper || sides || color;

	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons(atoi(argv[2]));
	a.sin_addr.s_addr = inet_addr(argv[1]);
	signal(SIGALRM, on_alarm);
	if ((s_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("prsend: socket");
		return 1;
	}
	alarm(60);
	if (connect(s_, (struct sockaddr *)&a, sizeof a) < 0) {
		fprintf(stderr, "prsend: %s:%s: %s\n", argv[1], argv[2], strerror(errno));
		return 1;
	}
	alarm(0);

	if (wrap) {
		const struct paper *pp = find_paper();

		if (puts_(UEL "@PJL JOB NAME=\"AMIX\"\r\n") < 0)
			return 1;
		if (pp) {
			sprintf(line, "@PJL SET PAPER=%s\r\n", pp->pjl);
			puts_(line);
		}
		if (sides && !strcmp(sides, "one"))
			puts_("@PJL SET DUPLEX=OFF\r\n");
		else if (sides) {
			puts_("@PJL SET DUPLEX=ON\r\n");
			puts_(!strcmp(sides, "two-short") ? "@PJL SET BINDING=SHORTEDGE\r\n"
							  : "@PJL SET BINDING=LONGEDGE\r\n");
		}
		if (color)
			puts_(!strcmp(color, "mono") ? "@PJL SET RENDERMODE=GRAYSCALE\r\n"
						     : "@PJL SET RENDERMODE=COLOR\r\n");
		if (ps)
			puts_("@PJL ENTER LANGUAGE=POSTSCRIPT\r\n");
		else if (pdf)
			puts_("@PJL ENTER LANGUAGE=PDF\r\n");
	}

	n = 0;
	if (wrap && ps) {
		/* the first line (%!PS...), then the page device settings */
		while (n < have && buf[n] != '\n' && buf[n] != '\r')
			n++;
		while (n < have && (buf[n] == '\n' || buf[n] == '\r'))
			n++;
		if (put(buf, n) < 0)
			return 1;
		if (find_paper()) {
			sprintf(line, "{ << /PageSize [%d %d] >> setpagedevice } stopped pop\n",
				find_paper()->w, find_paper()->h);
			puts_(line);
		}
		if (sides)
			puts_(!strcmp(sides, "one") ? "{ << /Duplex false >> setpagedevice } stopped pop\n"
			      : !strcmp(sides, "two-short")
				? "{ << /Duplex true /Tumble true >> setpagedevice } stopped pop\n"
				: "{ << /Duplex true /Tumble false >> setpagedevice } stopped pop\n");
		if (color && !strcmp(color, "mono"))
			puts_("{ << /ProcessColorModel /DeviceGray >> setpagedevice } stopped pop\n");
	}
	if (put(buf + n, have - n) < 0)
		return 1;
	while ((have = read(fd, buf, sizeof buf)) > 0)
		if (put(buf, have) < 0)
			return 1;
	close(fd);
	if (wrap && puts_(UEL "@PJL EOJ NAME=\"AMIX\"\r\n" UEL) < 0)
		return 1;
	shutdown(s_, 1);
	alarm(300);				/* the printer closes when it has it all */
	while (read(s_, buf, sizeof buf) > 0)
		;
	alarm(0);
	close(s_);
	return 0;
}
