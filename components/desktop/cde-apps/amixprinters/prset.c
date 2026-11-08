/*
 * prset.c -- the few lp administration steps amixprinters needs, for every
 * user.  Installed setuid root as /opt/amix/lib/amixprinters/prset.
 *
 *   prset add NAME IP PORT [description]   a network printer, raw TCP
 *   prset default NAME                     the system default destination
 *   prset remove NAME                      only printers prset added
 *   prset test NAME                        one test page
 *   prset set NAME OPTIONS                 the printer's defaults, e.g.
 *                                          paper=a4,sides=two-long,color=mono
 *
 * NAME is 1-14 of [a-z0-9_-], starting with a letter; IP a dotted quad;
 * PORT 1-65535.  "add" writes the interface script itself (the printer
 * takes PostScript, PDF and PCL as they are, so every file goes out
 * unchanged) and runs lpadmin, accept and enable with a fixed environment.
 * A printer prset added carries the marker line in its interface, so
 * "remove" never touches other printers.
 *
 * The defaults live in /etc/lp/amixprinters/NAME (one line, world readable);
 * the interface hands them and the job's own lp -o options (later ones win)
 * to prsend, which puts them into the job as PJL and, for PostScript, as
 * setpagedevice.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define LIBDIR  "/opt/amix/lib/amixprinters"
#define SENDER  LIBDIR "/prsend"
#define TESTPS  LIBDIR "/testpage.ps"
#define MARK    "# amixprinters: network printer"
#define OPTDIR  "/etc/lp/amixprinters"

static char *env[] = { "PATH=/usr/sbin:/usr/bin:/usr/lib/lp", "LANG=C", "TZ=GMT", NULL };

static int good_name(const char *s)
{
	int n = 0;

	if (!(*s >= 'a' && *s <= 'z'))
		return 0;
	for (; *s; s++, n++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-'))
			return 0;
	return n <= 14;
}

static int good_ip(const char *s)
{
	int parts = 0, v = -1;

	for (;; s++) {
		if (*s >= '0' && *s <= '9') {
			v = (v < 0 ? 0 : v) * 10 + (*s - '0');
			if (v > 255)
				return 0;
		} else if (*s == '.' || !*s) {
			if (v < 0)
				return 0;
			parts++;
			v = -1;
			if (!*s)
				break;
		} else
			return 0;
	}
	return parts == 4;
}

static int good_port(const char *s)
{
	long v = 0;

	if (!*s)
		return 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return 0;
		v = v * 10 + (*s - '0');
		if (v > 65535)
			return 0;
	}
	return v > 0;
}

/* run argv with the fixed environment; its exit status */
static int run(char **argv)
{
	pid_t p = fork();
	int st;

	if (p < 0)
		return -1;
	if (p == 0) {
		execve(argv[0], argv, env);
		perror(argv[0]);
		_exit(127);
	}
	while (waitpid(p, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int ours(const char *name)
{
	char path[100], line[200];
	FILE *f;
	int yes = 0;

	sprintf(path, "/etc/lp/interfaces/%s", name);
	if (!(f = fopen(path, "r")))
		return 0;
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, MARK, strlen(MARK)))
			yes = 1;
	fclose(f);
	return yes;
}

static int add(const char *name, const char *ip, const char *port, const char *desc)
{
	char tmp[40], d[100];
	int fd, i;
	FILE *f;
	char *lpadmin[] = { "/usr/sbin/lpadmin", "-p", (char *)name, "-v", "/dev/null",
			    "-i", tmp, "-D", d, NULL };
	char *accept[] = { "/usr/sbin/accept", (char *)name, NULL };
	char *enable[] = { "/usr/bin/enable", (char *)name, NULL };

	/* the description: printable, no quotes, at most 60 */
	for (i = 0; desc[i] && i < 60; i++)
		d[i] = (desc[i] >= ' ' && desc[i] < 127 && desc[i] != '"' && desc[i] != '\\') ? desc[i] : ' ';
	d[i] = 0;
	if (!d[0])
		sprintf(d, "network printer %s:%s", ip, port);

	umask(022);
	sprintf(tmp, "/tmp/prset.%ld", (long)getpid());	/* no mkstemp in AMIX's libc */
	unlink(tmp);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0700)) < 0 || !(f = fdopen(fd, "w"))) {
		perror("prset: interface");
		return 1;
	}
	fprintf(f, "#!/bin/sh\n%s %s port %s, by prset\n"
		"# lp calls: interface request-id user title copies options file ...\n"
		"# The printer takes PostScript, PDF and PCL as they are; prsend puts the\n"
		"# options (the defaults in %s/%s, then lp -o) in front as PJL.\n"
		"copies=$4\nopts=$5\nshift 5\n"
		"defs=`cat %s/%s 2>/dev/null`\n"
		"[ -n \"$copies\" ] && [ \"$copies\" -ge 1 ] 2>/dev/null || copies=1\n"
		"i=0\nwhile [ $i -lt $copies ]\ndo\n"
		"\tfor f\n\tdo\n\t\t%s %s %s \"$f\" \"$defs,$opts\" || exit 1\n\tdone\n"
		"\ti=`expr $i + 1`\ndone\nexit 0\n", MARK, ip, port, OPTDIR, name, OPTDIR, name,
		SENDER, ip, port);
	if (fclose(f) != 0 || chmod(tmp, 0755) != 0) {
		perror("prset: interface");
		unlink(tmp);
		return 1;
	}
	i = run(lpadmin);
	unlink(tmp);
	if (i != 0) {
		fprintf(stderr, "prset: lpadmin failed (%d)\n", i);
		return 1;
	}
	/* both warn and fail when the printer is already accepting/enabled
	   (adding it again replaces the interface only): not an error */
	run(accept);
	run(enable);
	return 0;
}

/* the keys prsend knows, and their values */
static const char *const allowed[][6] = {
	{ "paper", "a4", "a3", "a5", "letter", "legal" },
	{ "sides", "one", "two-long", "two-short", NULL, NULL },
	{ "color", "color", "mono", NULL, NULL, NULL },
};

static int set_options(const char *name, const char *opts)
{
	char buf[200], path[100], tmp[110], kv[40], *eq;
	const char *p = opts;
	int n = 0, i, j, fd;

	buf[0] = 0;
	while (*p) {
		int l = strcspn(p, ",");

		if (l >= (int)sizeof kv || !(l > 0)) {
			fprintf(stderr, "prset: bad option list: %s\n", opts);
			return 2;
		}
		memcpy(kv, p, l);
		kv[l] = 0;
		p += l + (p[l] == ',');
		if (!(eq = strchr(kv, '='))) {
			fprintf(stderr, "prset: not key=value: %s\n", kv);
			return 2;
		}
		*eq++ = 0;
		for (i = 0; i < 3 && strcmp(allowed[i][0], kv); i++)
			;
		for (j = 1; i < 3 && j < 6 && allowed[i][j] && strcmp(allowed[i][j], eq); j++)
			;
		if (i == 3 || j == 6 || !allowed[i][j]) {
			fprintf(stderr, "prset: unknown option %s=%s\n", kv, eq);
			return 2;
		}
		n += sprintf(buf + n, "%s%s=%s", n ? "," : "", kv, eq);
	}
	umask(022);
	mkdir(OPTDIR, 0755);
	sprintf(path, "%s/%s", OPTDIR, name);
	sprintf(tmp, "%s.new", path);
	unlink(tmp);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
		perror(tmp);
		return 1;
	}
	strcat(buf, "\n");
	if (write(fd, buf, strlen(buf)) != (int)strlen(buf) || close(fd) != 0 || rename(tmp, path) != 0) {
		perror(path);
		unlink(tmp);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	uid_t user = getuid();

	/* lp's commands check the real user id: root for the admin steps, the
	   user's own for the test page (below) */
	if (argc >= 2 && strcmp(argv[1], "test") && setuid(0) != 0) {
		perror("prset: setuid");
		return 1;
	}
	if (argc >= 3 && !good_name(argv[2])) {
		fprintf(stderr, "prset: bad printer name: %s\n", argv[2]);
		return 2;
	}
	if ((argc == 5 || argc == 6) && !strcmp(argv[1], "add")) {
		if (!good_ip(argv[3]) || !good_port(argv[4])) {
			fprintf(stderr, "prset: bad address %s:%s\n", argv[3], argv[4]);
			return 2;
		}
		{
			char path[100];
			struct stat st;

			sprintf(path, "/etc/lp/printers/%s", argv[2]);
			if (stat(path, &st) == 0 && !ours(argv[2])) {
				fprintf(stderr, "prset: %s exists and was not added by amixprinters\n", argv[2]);
				return 1;
			}
		}
		return add(argv[2], argv[3], argv[4], argc == 6 ? argv[5] : "");
	}
	if (argc == 3 && !strcmp(argv[1], "default")) {
		char *a[] = { "/usr/sbin/lpadmin", "-d", argv[2], NULL };

		return run(a) == 0 ? 0 : 1;
	}
	if (argc == 3 && !strcmp(argv[1], "remove")) {
		char *r[] = { "/usr/sbin/reject", argv[2], NULL };
		char *x[] = { "/usr/sbin/lpadmin", "-x", argv[2], NULL };

		if (!ours(argv[2])) {
			fprintf(stderr, "prset: %s was not added by amixprinters\n", argv[2]);
			return 1;
		}
		run(r);
		if (run(x) != 0)
			return 1;
		{
			char path[100];

			sprintf(path, "%s/%s", OPTDIR, argv[2]);
			unlink(path);
		}
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "set")) {
		if (!ours(argv[2])) {
			fprintf(stderr, "prset: %s was not added by amixprinters\n", argv[2]);
			return 1;
		}
		return set_options(argv[2], argv[3]);
	}
	if (argc == 3 && !strcmp(argv[1], "test")) {
		char *a[] = { "/usr/bin/lp", "-d", argv[2], "-t", "AMIX test page", TESTPS, NULL };

		setuid(user);			/* the user's own job */
		return run(a) == 0 ? 0 : 1;
	}
	fprintf(stderr, "usage: prset add NAME IP PORT [description] | default NAME | remove NAME | test NAME\n"
			"       prset set NAME paper=a4|a3|a5|letter|legal,sides=one|two-long|two-short,color=color|mono\n");
	return 2;
}
