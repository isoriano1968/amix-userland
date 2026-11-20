/*
 * swrun.c -- the AMIX Software Center's root helper, after the root
 * password.  Installed setuid root as /opt/amix/lib/amixsoftware/swrun.
 *
 *   swrun APKG [-n] VERB [NAME ...]                 apkg (amigaux)
 *   swrun pkg PKG [--min-free MB] [-n] install|update|uninstall [NAME ...]
 *   swrun pkg PKG refresh                           IPS pkg (amix-pkg)
 *   swrun pkg PKG set-publisher -O|-g|-G|--enable-origin|--disable-origin URL
 *   swrun apkgconf repo|cachedir|nameserver VALUE   one line of /etc/apkg.conf
 *   swrun sysconf                                   /etc/amixsoftware.conf
 *
 * The root password comes on the first line of stdin; the caller's real uid
 * 0 needs none.  It is checked against /etc/shadow with crypt(3).  sysconf
 * reads the new file after it, key=value lines of the Software Center's
 * known keys only.
 *
 * apkg verbs: update, install, upgrade, remove, download; its NAMEs are of
 * [A-Za-z0-9._+-].  pkg NAMEs may also hold / @ , : (FMRIs).  At most 32.
 * URLs: http://, file:// or an absolute path, of [A-Za-z0-9._~:/@%+-].
 *
 * APKG and PKG are the programs chosen in the Software Center's
 * preferences.  Since they then run as root, each must be an absolute path
 * to a regular file (not a symbolic link) owned by root and writable by
 * nobody else, in a directory owned by root and writable by nobody else.
 *
 * They run with a fixed environment and stdin on /dev/null (apkg drives
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
#define APKG_CONF "/etc/apkg.conf"
#define SYS_CONF  "/etc/amixsoftware.conf"

static char *env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C", "TZ=GMT", "HOME=/", NULL };

/* the keys /etc/amixsoftware.conf may hold (amixsoftware.c writes them) */
static const char *sys_keys[] = {
	"ips_on", "ips_path", "apkg_on", "apkg", "prefer", "show_both", "min_free", "confirm",
	"dryrun", "show060", "ask_original", "update_at_start", NULL
};

static void die(const char *msg)
{
	fprintf(stderr, "swrun: %s\n", msg);
	exit(2);
}

static int in_set(int c, const char *extra)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
	       (c && strchr(extra, c));
}

static int good_chars(const char *s, const char *extra, int max)
{
	int n = 0;

	for (; *s; s++, n++)
		if (!in_set((unsigned char)*s, extra))
			return 0;
	return n > 0 && n <= max;
}

static int good_name(const char *s)
{
	return good_chars(s, "._+-", 32);
}

static int good_fmri(const char *s)
{
	return s[0] != '-' && good_chars(s, "._+-/@,:", 128);
}

static int good_url(const char *s)
{
	if (strncmp(s, "http://", 7) && strncmp(s, "file://", 7) && s[0] != '/')
		return 0;
	return good_chars(s, "._~:/@%+-", 250);
}

static int good_verb(const char *s)
{
	return !strcmp(s, "update") || !strcmp(s, "install") || !strcmp(s, "upgrade") ||
	       !strcmp(s, "remove") || !strcmp(s, "download");
}

/* a system account's (root, or bin and sys, which own AMIX's own /usr and
   /usr/bin as SVR4 has them), and nobody else may write it */
static int safe(const struct stat *st)
{
	static int got = 0;
	static uid_t bin = 0, sys = 0;

	if (!got) {
		struct passwd *p;

		if ((p = getpwnam("bin")) != NULL)
			bin = p->pw_uid;
		if ((p = getpwnam("sys")) != NULL)
			sys = p->pw_uid;
		got = 1;
	}
	return (st->st_uid == 0 || (bin && st->st_uid == bin) || (sys && st->st_uid == sys)) &&
	    !(st->st_mode & (S_IWGRP | S_IWOTH));
}

static void check_program(const char *path)
{
	struct stat st;
	char dir[256], *slash;

	if (path[0] != '/' || strlen(path) >= sizeof dir)
		die("the program must be an absolute path");
	if (lstat(path, &st) < 0)
		die("the program is not there");
	if (!S_ISREG(st.st_mode))
		die("the program must be a regular file, not a link");
	if (!safe(&st) || !(st.st_mode & S_IXUSR))
		die("the program must be a system account's (root, bin, sys) and writable by nobody else");
	/* every directory up to /: one another user could write would let him
	   put another program in its place */
	strcpy(dir, path);
	for (;;) {
		slash = strrchr(dir, '/');
		if (slash == dir)
			slash[1] = 0;
		else
			*slash = 0;
		if (stat(dir, &st) < 0 || !S_ISDIR(st.st_mode) || !safe(&st))
			die("the program's directories must be a system account's (root, bin, sys) "
			    "and writable by nobody else");
		if (!strcmp(dir, "/"))
			break;
	}
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

static void become_root(void)
{
	if (setgid(0) < 0 || setuid(0) < 0)
		die("cannot become root (is swrun setuid root?)");
}

static void run(char **args)
{
	int fd;

	become_root();
	if ((fd = open("/dev/null", O_RDONLY)) >= 0 && fd != 0) {
		dup2(fd, 0);
		close(fd);
	}
	execve(args[0], args, env);
	perror(args[0]);
	exit(127);
}

/* write path from text: path.new, then rename (mode 644, root's) */
static void replace_file(const char *path, const char *text)
{
	char tmp[300];
	FILE *f;

	sprintf(tmp, "%s.new", path);
	umask(022);
	if (!(f = fopen(tmp, "w")))
		die("cannot write the file");
	fputs(text, f);
	if (fclose(f) != 0 || chmod(tmp, 0644) < 0 || rename(tmp, path) < 0) {
		unlink(tmp);
		die("cannot write the file");
	}
}

/* apkgconf KEY VALUE: that key's line replaced (or added), every other line kept */
static int apkgconf(const char *key, const char *value)
{
	static char text[16384];
	char line[512];
	FILE *f;
	size_t kl = strlen(key), n = 0;
	int done = 0;

	if (strcmp(key, "repo") && strcmp(key, "cachedir") && strcmp(key, "nameserver"))
		die("apkgconf: repo, cachedir or nameserver");
	if (!strcmp(key, "repo")) {
		/* apkg's own repository may be https (apkg fetches it, not pkg) */
		int ok = !strncmp(value, "https://", 8) ? good_chars(value + 8, "._~:/@%+-", 240)
							: good_url(value);

		if (!ok)
			die("apkgconf: repo is http://, https:// or file://");
	} else if (!good_chars(value, "._/-:", 200))
		die("apkgconf: not a good value");
	check_password();
	become_root();
	text[0] = 0;
	if ((f = fopen(APKG_CONF, "r"))) {
		while (fgets(line, sizeof line, f)) {
			const char *put = line;
			char mine[512];

			if (!strncmp(line, key, kl) && line[kl] == '=') {
				if (done)
					continue;
				sprintf(mine, "%s=%s\n", key, value);
				put = mine;
				done = 1;
			}
			if (n + strlen(put) >= sizeof text - 1)
				die("apkgconf: " APKG_CONF " is too long");
			strcpy(text + n, put);
			n += strlen(put);
		}
		fclose(f);
	}
	if (!done) {
		if (n + kl + strlen(value) + 3 >= sizeof text)
			die("apkgconf: " APKG_CONF " is too long");
		sprintf(text + n, "%s=%s\n", key, value);
	}
	replace_file(APKG_CONF, text);
	return 0;
}

/* sysconf: the lines after the password, known keys only */
static int write_sysconf(void)
{
	static char text[8192];
	char line[400];
	size_t n;
	int c, k, len;

	check_password();
	strcpy(text, "# AMIX Software Center: settings for everyone (users' own: ~/.amixsoftware)\n");
	n = strlen(text);
	for (;;) {
		len = 0;
		while ((c = getchar()) != EOF && c != '\n')
			if (len < (int)sizeof line - 1)
				line[len++] = c;
		line[len] = 0;
		if (len) {
			char *eq = strchr(line, '=');

			if (line[0] == '#')
				goto next;
			if (!eq)
				die("sysconf: not a key=value line");
			*eq = 0;
			for (k = 0; sys_keys[k] && strcmp(sys_keys[k], line); k++)
				;
			if (!sys_keys[k])
				die("sysconf: unknown key");
			if (eq[1] && !good_chars(eq + 1, "._/-", 250))
				die("sysconf: not a good value");
			*eq = '=';
			if (n + len + 2 >= sizeof text)
				die("sysconf: too long");
			strcpy(text + n, line);
			n += len;
			text[n++] = '\n';
			text[n] = 0;
		}
	next:
		if (c == EOF)
			break;
	}
	become_root();
	replace_file(SYS_CONF, text);
	return 0;
}

static int pkg(int argc, char **argv)
{
	char *args[MAXNAMES + 10];
	int i = 3, n = 0;

	if (argc < 4)
		die("usage: swrun pkg PKG ...");
	check_program(argv[2]);
	args[n++] = argv[2];
	if (!strcmp(argv[i], "--min-free")) {
		if (i + 1 >= argc || !good_chars(argv[i + 1], "", 6))
			die("--min-free MB");
		args[n++] = argv[i++];
		args[n++] = argv[i++];
	}
	if (i >= argc)
		die("no command");
	if (!strcmp(argv[i], "refresh")) {
		if (i + 1 != argc)
			die("refresh takes nothing more");
		args[n++] = argv[i];
	} else if (!strcmp(argv[i], "set-publisher")) {
		const char *o = i + 1 < argc ? argv[i + 1] : "";

		if (i + 3 != argc || (strcmp(o, "-O") && strcmp(o, "-g") && strcmp(o, "-G") &&
		    strcmp(o, "--enable-origin") && strcmp(o, "--disable-origin")))
			die("set-publisher -O|-g|-G|--enable-origin|--disable-origin URL");
		if (!good_url(argv[i + 2]))
			die("not a good origin (http://, file:// or a folder)");
		args[n++] = argv[i];
		args[n++] = argv[i + 1];
		args[n++] = argv[i + 2];
		args[n++] = "amix";
	} else {
		if (!strcmp(argv[i], "-n"))
			args[n++] = argv[i++];
		if (i >= argc || (strcmp(argv[i], "install") && strcmp(argv[i], "update") &&
		    strcmp(argv[i], "uninstall")))
			die("install, update or uninstall");
		/* pkg's own -n goes after the verb (given before or after it) */
		if (n > 1 && !strcmp(args[n - 1], "-n")) {
			args[n - 1] = argv[i++];
			args[n++] = "-n";
		} else {
			args[n++] = argv[i++];
			if (i < argc && !strcmp(argv[i], "-n"))
				args[n++] = argv[i++];
		}
		if (argc - i > MAXNAMES)
			die("too many packages");
		for (; i < argc; i++) {
			if (!good_fmri(argv[i]))
				die("bad package name");
			args[n++] = argv[i];
		}
	}
	args[n] = NULL;
	check_password();
	run(args);
	return 127;
}

int main(int argc, char **argv)
{
	char *args[MAXNAMES + 5];
	int i = 2, n = 0;

	if (argc >= 2 && !strcmp(argv[1], "pkg"))
		return pkg(argc, argv);
	if (argc == 4 && !strcmp(argv[1], "apkgconf"))
		return apkgconf(argv[2], argv[3]);
	if (argc == 2 && !strcmp(argv[1], "sysconf"))
		return write_sysconf();
	if (argc < 3)
		die("usage: swrun APKG [-n] VERB [NAME ...] | pkg PKG ... | apkgconf KEY VALUE | sysconf");
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
	run(args);
	return 127;
}
