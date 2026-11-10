/*
 * swrun.c -- run apkg as root for the AMIX Software Center, after the root
 * password.  Installed setuid root as /opt/amix/lib/amixsoftware/swrun.
 *
 *   swrun APKG [-n] VERB [NAME ...]        the root password on stdin
 *
 * VERB is update, install, upgrade, remove or download; -n is apkg's dry
 * run.  NAMEs are package names of [A-Za-z0-9._+-], at most 32 of them.
 * The caller's real uid 0 needs no password; anyone else gives root's on
 * the first line of stdin, checked against /etc/shadow with crypt(3).
 *
 * APKG is the apkg program the user chose in the Software Center's
 * settings.  Since it then runs as root, it must be an absolute path to a
 * regular file (not a symbolic link) owned by root and writable by nobody
 * else, in a directory owned by root and writable by nobody else.
 *
 * apkg runs with a fixed environment and stdin on /dev/null (it drives
 * pkgadd and pkgrm, which must never wait for a terminal).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <shadow.h>
#include <sys/types.h>
#include <sys/stat.h>

extern char *crypt(const char *, const char *);

#define MAXNAMES 32

static char *env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C", "TZ=GMT", "HOME=/", NULL };

static void die(const char *msg)
{
	fprintf(stderr, "swrun: %s\n", msg);
	exit(2);
}

static int good_name(const char *s)
{
	int n = 0;

	for (; *s; s++, n++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
		      *s == '.' || *s == '_' || *s == '+' || *s == '-'))
			return 0;
	return n > 0 && n <= 32;
}

static int good_verb(const char *s)
{
	return !strcmp(s, "update") || !strcmp(s, "install") || !strcmp(s, "upgrade") ||
	       !strcmp(s, "remove") || !strcmp(s, "download");
}

/* root's, and nobody else may write it */
static int safe(const struct stat *st)
{
	return st->st_uid == 0 && !(st->st_mode & (S_IWGRP | S_IWOTH));
}

static void check_program(const char *path)
{
	struct stat st;
	char dir[256], *slash;

	if (path[0] != '/' || strlen(path) >= sizeof dir)
		die("the apkg program must be an absolute path");
	if (lstat(path, &st) < 0)
		die("the apkg program is not there");
	if (!S_ISREG(st.st_mode))
		die("the apkg program must be a regular file, not a link");
	if (!safe(&st) || !(st.st_mode & S_IXUSR))
		die("the apkg program must be root's and writable by nobody else");
	strcpy(dir, path);
	slash = strrchr(dir, '/');
	if (slash == dir)
		slash[1] = 0;
	else
		*slash = 0;
	if (stat(dir, &st) < 0 || !S_ISDIR(st.st_mode) || !safe(&st))
		die("the apkg program's directory must be root's and writable by nobody else");
}

static void check_password(void)
{
	char pw[128];
	struct spwd *sp;
	char *hash, *c;
	int n;

	if (getuid() == 0)
		return;
	n = 0;
	while (n < (int)sizeof pw - 1 && read(0, pw + n, 1) == 1 && pw[n] != '\n')
		n++;
	pw[n] = 0;
	if (!(sp = getspnam("root")))
		die("cannot read root's password entry");
	hash = sp->sp_pwdp;
	if (hash && hash[0]) {
		c = crypt(pw, hash);
		if (!c || strcmp(c, hash)) {
			memset(pw, 0, sizeof pw);
			sleep(2);
			fprintf(stderr, "swrun: wrong root password\n");
			exit(3);
		}
	}
	memset(pw, 0, sizeof pw);
}

int main(int argc, char **argv)
{
	char *args[MAXNAMES + 5];
	int i = 2, n = 0, fd;

	if (argc < 3)
		die("usage: swrun APKG [-n] VERB [NAME ...]");
	check_program(argv[1]);
	args[n++] = argv[1];
	if (!strcmp(argv[i], "-n"))
		args[n++] = argv[i++];
	if (i >= argc || !good_verb(argv[i]))
		die("unknown verb");
	args[n++] = argv[i++];
	if (argc - i > MAXNAMES)
		die("too many packages");
	for (; i < argc; i++) {
		if (!good_name(argv[i]))
			die("bad package name");
		args[n++] = argv[i];
	}
	args[n] = NULL;
	check_password();
	if (setgid(0) < 0 || setuid(0) < 0)
		die("cannot become root (is swrun setuid root?)");
	if ((fd = open("/dev/null", O_RDONLY)) >= 0 && fd != 0) {
		dup2(fd, 0);
		close(fd);
	}
	execve(args[0], args, env);
	perror(args[0]);
	return 127;
}
