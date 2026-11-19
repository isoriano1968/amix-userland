/*
 * vscode-agent -- runs programs for VS Code on AMIX.
 *
 * Started at boot by /etc/init.d/vscode-agent.  It dials out to the VS Code
 * extension (an emulator's NAT lets nothing dial in), proves with a shared
 * secret that both ends are who they claim (HMAC-SHA-256 challenge and
 * response, the secret never crosses the wire), then takes programs into
 * its session folder under /tmp and runs them on a pty as the configured
 * user.  The protocol is in ../README.md.
 *
 * Root never touches a file the user can change: the session folder and the
 * programs in it belong to root; the user's working folder (run/) is read
 * and removed only by a child that has become the user.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifdef __amix__
#include <stropts.h>
/* from <sys/sysm68k.h>, which hides them unless m68k is defined (gcc 14 has __m68k__) */
#define _m68k_LIMUSER	95
#define EUA_ADD_USER	4
int sysm68k(int, ...);
/* AMIX's headers do not declare these */
int socket(int, int, int);
int connect(int, const struct sockaddr *, int);
int grantpt(int);
int unlockpt(int);
char *ptsname(int);
#endif

#include "agent.h"

#ifndef S_ISLNK
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef WCOREDUMP
#define WCOREDUMP(s)	((s) & 0200)
#endif

#define CONF_DEFAULT	"/etc/vscode-agent.conf"
#define PID_FILE	"/var/run/vscode-agent.pid"
#define MAX_CONNECT	8
#define MAX_RUNS	8
#define MAX_LINE	8192
#define MAX_TOKENS	256
#define AUTH_SECONDS	20

struct conf {
	char *connect[MAX_CONNECT];
	int nconnect;
	char user[64];
	char secret[256];
	char tmpdir[256];
	char log[256];
	long reserve_kb;
	int retry;
};

struct run {
	int used;
	char id[32];
	pid_t pid;
	int master, slave;
	int exited, status;
};

static struct conf C;
static struct run runs[MAX_RUNS];
static int foreground, sock = -1, dead, authed;
static volatile sig_atomic_t stopping;
static FILE *logf;
static char nonce_a[33];
static char sess[300], rundir[320];
static int is_root;
static uid_t ruid;
static gid_t rgid;
static char rhome[256], rshell[128];

/* the socket input and the payload being received */
static char *ib;
static size_t ilen, icap;
static long need;		/* payload bytes still to come */
static enum { SINK_NONE, SINK_PUT, SINK_IN } sink;
static int sinkfd = -1, sinkrun = -1;
static struct sha256 sinksha;
static char sinkname[80], sinkwant[65], sinkerr[128];
static int sinkmode;

/* --- small helpers ------------------------------------------------------ */

static void
logmsg(const char *fmt, ...)
{
	va_list ap;
	char t[32];
	time_t now = time(NULL);
	FILE *f = foreground ? stderr : logf;

	if (f == NULL)
		return;
	strftime(t, sizeof t, "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(f, "%s vscode-agent: ", t);
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fflush(f);
}

static void
sendall(const void *p, size_t n)
{
	const char *c = p;

	while (n > 0 && !dead) {
		ssize_t w = write(sock, c, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
				struct pollfd pf;

				pf.fd = sock;
				pf.events = POLLOUT;
				poll(&pf, 1, 1000);
				continue;
			}
			logmsg("send: %s", strerror(errno));
			dead = 1;
			return;
		}
		c += w;
		n -= w;
	}
}

static void
sendf(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n >= sizeof buf)
		n = sizeof buf - 1;
	sendall(buf, n);
}

/* %XX for spaces, control characters, '%' and bytes from 0x7f */
static void
enc(const char *s, char *out, size_t size)
{
	static const char x[] = "0123456789ABCDEF";
	size_t o = 0;

	for (; *s && o + 4 < size; s++) {
		unsigned char c = *s;

		if (c <= ' ' || c == '%' || c >= 0x7f) {
			out[o++] = '%';
			out[o++] = x[c >> 4];
			out[o++] = x[c & 15];
		} else
			out[o++] = c;
	}
	out[o] = 0;
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void
dec(char *s)
{
	char *o = s;

	for (; *s; s++) {
		if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
			*o++ = (char)(hexval(s[1]) << 4 | hexval(s[2]));
			s += 2;
		} else
			*o++ = *s;
	}
	*o = 0;
}

static int
split(char *line, char **tok, int max)
{
	int n = 0;
	char *p = line;

	while (*p && n < max) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		tok[n++] = p;
		while (*p && *p != ' ')
			p++;
		if (*p)
			*p++ = 0;
	}
	return n;
}

/* a file or run name: letters, digits, . _ + -, not starting with a dot */
static int
goodname(const char *s, size_t max)
{
	size_t n = strlen(s), i;

	if (n == 0 || n > max || s[0] == '.')
		return 0;
	for (i = 0; i < n; i++) {
		char c = s[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		    c == '.' || c == '_' || c == '+' || c == '-'))
			return 0;
	}
	return 1;
}

static int
ishex(const char *s, size_t min, size_t max)
{
	size_t n = strlen(s), i;

	if (n < min || n > max)
		return 0;
	for (i = 0; i < n; i++)
		if (hexval(s[i]) < 0 || (s[i] >= 'A' && s[i] <= 'F'))
			return 0;
	return 1;
}

/* equal without telling how much of it was */
static int
same(const char *a, const char *b)
{
	size_t n = strlen(a), i;
	unsigned d = 0;

	if (n != strlen(b))
		return 0;
	for (i = 0; i < n; i++)
		d |= (unsigned char)a[i] ^ (unsigned char)b[i];
	return d == 0;
}

static long
free_kb(const char *path)
{
	struct statvfs v;
	double unit;

	if (statvfs(path, &v) != 0)
		return -1;
	unit = v.f_frsize ? (double)v.f_frsize : (double)v.f_bsize;
	return (long)((double)v.f_bavail * unit / 1024.0);
}

/* --- the configuration -------------------------------------------------- */

static int
read_conf(const char *path)
{
	FILE *f;
	char line[512];
	struct stat st;
	int ln = 0;

	memset(&C, 0, sizeof C);
	strcpy(C.tmpdir, "/tmp");
	C.reserve_kb = 100L * 1024;
	C.retry = 10;
	if ((f = fopen(path, "r")) == NULL) {
		fprintf(stderr, "vscode-agent: %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (is_root && (fstat(fileno(f), &st) != 0 || st.st_uid != 0 || (st.st_mode & 077))) {
		fprintf(stderr, "vscode-agent: %s must belong to root and be readable by root "
		    "only (chmod 600): it holds the secret\n", path);
		fclose(f);
		return -1;
	}
	while (fgets(line, sizeof line, f)) {
		char *t[3], *h;
		int n;

		ln++;
		if ((h = strchr(line, '#')) != NULL)
			*h = 0;
		line[strcspn(line, "\r\n")] = 0;
		for (h = line; *h; h++)
			if (*h == '\t')
				*h = ' ';
		if ((n = split(line, t, 3)) == 0)
			continue;
		if (n != 2) {
			fprintf(stderr, "vscode-agent: %s:%d: \"key value\" expected\n", path, ln);
			fclose(f);
			return -1;
		}
		if (strcmp(t[0], "connect") == 0 && C.nconnect < MAX_CONNECT)
			C.connect[C.nconnect++] = strdup(t[1]);
		else if (strcmp(t[0], "user") == 0)
			snprintf(C.user, sizeof C.user, "%s", t[1]);
		else if (strcmp(t[0], "secret") == 0)
			snprintf(C.secret, sizeof C.secret, "%s", t[1]);
		else if (strcmp(t[0], "tmpdir") == 0)
			snprintf(C.tmpdir, sizeof C.tmpdir, "%s", t[1]);
		else if (strcmp(t[0], "log") == 0)
			snprintf(C.log, sizeof C.log, "%s", t[1]);
		else if (strcmp(t[0], "reserve-mb") == 0)
			C.reserve_kb = atol(t[1]) * 1024;
		else if (strcmp(t[0], "retry") == 0)
			C.retry = atoi(t[1]) > 0 ? atoi(t[1]) : 10;
		else {
			fprintf(stderr, "vscode-agent: %s:%d: unknown key %s\n", path, ln, t[0]);
			fclose(f);
			return -1;
		}
	}
	fclose(f);
	if (C.nconnect == 0 || strlen(C.secret) < 16) {
		fprintf(stderr, "vscode-agent: %s needs a connect line and a secret of 16 "
		    "characters or more\n", path);
		return -1;
	}
	if (is_root) {
		struct passwd *pw;

		if (!C.user[0] || (pw = getpwnam(C.user)) == NULL || pw->pw_uid == 0) {
			fprintf(stderr, "vscode-agent: %s needs \"user NAME\", a user other than "
			    "root, to run the programs as\n", path);
			return -1;
		}
		ruid = pw->pw_uid;
		rgid = pw->pw_gid;
		snprintf(rhome, sizeof rhome, "%s", pw->pw_dir);
		snprintf(rshell, sizeof rshell, "%s", pw->pw_shell[0] ? pw->pw_shell : "/bin/sh");
	} else {
		struct passwd *pw = getpwuid(getuid());

		ruid = getuid();
		rgid = getgid();
		snprintf(C.user, sizeof C.user, "%s", pw ? pw->pw_name : "user");
		snprintf(rhome, sizeof rhome, "%s", pw ? pw->pw_dir : "/");
		snprintf(rshell, sizeof rshell, "%s", pw && pw->pw_shell[0] ? pw->pw_shell : "/bin/sh");
	}
	return 0;
}

/* --- as the user -------------------------------------------------------- */

static int
become_user(void)
{
	if (!is_root)
		return 0;
	if (setgid(rgid) != 0 || initgroups(C.user, rgid) != 0 || setuid(ruid) != 0)
		return -1;
	return 0;
}

static void
rmtree(const char *path, int top)
{
	struct stat st;
	DIR *d;
	struct dirent *e;
	char p[1024];

	if (lstat(path, &st) != 0)
		return;
	if (!S_ISDIR(st.st_mode)) {
		unlink(path);
		return;
	}
	chmod(path, 0700);
	if ((d = opendir(path)) != NULL) {
		while ((e = readdir(d)) != NULL) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
				continue;
			snprintf(p, sizeof p, "%s/%s", path, e->d_name);
			rmtree(p, 0);
		}
		closedir(d);
	}
	if (!top)
		rmdir(path);
}

/* empties run/ as the user: root would follow the user's links */
static void
empty_rundir(void)
{
	pid_t p;
	int st;

	if (!rundir[0])
		return;
	if ((p = fork()) == 0) {
		if (become_user() != 0)
			_exit(1);
		rmtree(rundir, 1);
		_exit(0);
	}
	if (p > 0)
		while (waitpid(p, &st, 0) < 0 && errno == EINTR)
			;
}

/* --- the session folder ------------------------------------------------- */

static int
make_session(void)
{
	int i;

	for (i = 0; i < 100; i++) {
		struct stat st;

		if (i == 0)
			snprintf(sess, sizeof sess, "%s/vscode-agent.%ld", C.tmpdir, (long)getpid());
		else
			snprintf(sess, sizeof sess, "%s/vscode-agent.%ld.%d", C.tmpdir, (long)getpid(), i);
		if (mkdir(sess, 0755) == 0)
			break;
		if (errno != EEXIST || (lstat(sess, &st) == 0 && !S_ISDIR(st.st_mode)))
			continue;
	}
	if (i == 100) {
		logmsg("cannot make a session folder in %s: %s", C.tmpdir, strerror(errno));
		sess[0] = 0;
		return -1;
	}
	chmod(sess, 0755);		/* whatever the umask */
	snprintf(rundir, sizeof rundir, "%s/run", sess);
	if (mkdir(rundir, 0700) != 0 || (is_root && chown(rundir, ruid, rgid) != 0)) {
		logmsg("%s: %s", rundir, strerror(errno));
		rmdir(rundir);
		rmdir(sess);
		sess[0] = rundir[0] = 0;
		return -1;
	}
	return 0;
}

static void
remove_session(void)
{
	DIR *d;
	struct dirent *e;
	char p[1024];

	if (!sess[0])
		return;
	empty_rundir();
	if (rmdir(rundir) != 0 && errno != ENOENT)
		logmsg("%s left behind: %s", rundir, strerror(errno));
	/* the rest is root's own: the programs that came with PUT */
	if ((d = opendir(sess)) != NULL) {
		while ((e = readdir(d)) != NULL) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 ||
			    strcmp(e->d_name, "run") == 0)
				continue;
			snprintf(p, sizeof p, "%s/%s", sess, e->d_name);
			unlink(p);
		}
		closedir(d);
	}
	if (rmdir(sess) != 0)
		logmsg("%s left behind: %s", sess, strerror(errno));
	sess[0] = rundir[0] = 0;
}

/* --- the programs ------------------------------------------------------- */

static struct run *
find_run(const char *id)
{
	int i;

	for (i = 0; i < MAX_RUNS; i++)
		if (runs[i].used && strcmp(runs[i].id, id) == 0)
			return &runs[i];
	return NULL;
}

static void
set_size(int fd, int cols, int rows)
{
#ifdef TIOCSWINSZ
	struct winsize ws;

	memset(&ws, 0, sizeof ws);
	ws.ws_col = cols;
	ws.ws_row = rows;
	ioctl(fd, TIOCSWINSZ, &ws);
#endif
}

/* the modes of a terminal after "stty sane": a freshly pushed ldterm on AMIX
 * has no CR-to-NL mapping, so a line typed in VS Code (which sends CR)
 * would never end */
static void
sane(int fd)
{
	struct termios t;

	if (tcgetattr(fd, &t) != 0)
		return;
	t.c_iflag |= BRKINT | ICRNL | IXON;
	t.c_iflag &= ~(INLCR | IGNCR | IUCLC | IXOFF);
	t.c_oflag |= OPOST | ONLCR;
	t.c_oflag &= ~(OCRNL | ONLRET);
	t.c_cflag &= ~CSIZE;
	t.c_cflag |= CS8 | CREAD | HUPCL;
	t.c_lflag |= ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
	t.c_lflag &= ~(ECHONL | NOFLSH);
	t.c_cc[VINTR] = 3;		/* ^C */
	t.c_cc[VQUIT] = 0x1c;		/* ^\ */
	t.c_cc[VERASE] = 0x7f;		/* what VS Code's Backspace sends */
	t.c_cc[VKILL] = 0x15;		/* ^U */
	t.c_cc[VEOF] = 4;		/* ^D */
#ifdef VSUSP
	t.c_cc[VSUSP] = 0x1a;		/* ^Z */
#endif
	cfsetispeed(&t, B38400);
	cfsetospeed(&t, B38400);
	tcsetattr(fd, TCSANOW, &t);
}

/* xterm-256color (sysutils/terminfo, which the package requires) is what VS
 * Code's terminal is; stock AMIX's 1993 xterm entry is the fallback */
static const char *
term_name(void)
{
	return access("/usr/share/lib/terminfo/x/xterm-256color", R_OK) == 0
	    ? "xterm-256color" : "xterm";
}

static void
setenv_list(char **env, int *n, int max, const char *name, const char *value)
{
	size_t l = strlen(name);
	char *s;
	int i;

	if ((s = malloc(l + strlen(value) + 2)) == NULL)
		return;
	sprintf(s, "%s=%s", name, value);
	for (i = 0; i < *n; i++)
		if (strncmp(env[i], name, l) == 0 && env[i][l] == '=') {
			env[i] = s;
			return;
		}
	if (*n < max - 1)
		env[(*n)++] = s;
	env[*n] = NULL;
}

static void
child(const char *slavename, char **argv, char **extra, int nextra)
{
	char *env[64], path[600], *e;
	int n = 0, fd, i;
	struct stat st;

	setsid();
	/* a session leader without a terminal gets this one as its own */
	if ((fd = open(slavename, O_RDWR)) < 0)
		_exit(126);
#ifdef TIOCSCTTY
	ioctl(fd, TIOCSCTTY, 0);
#endif
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	for (i = 3; i < 64; i++)
		close(i);
	if (become_user() != 0) {
		fprintf(stderr, "vscode-agent: cannot become %s: %s\r\n", C.user, strerror(errno));
		_exit(126);
	}
	if (chdir(rundir) != 0) {
		fprintf(stderr, "vscode-agent: %s: %s\r\n", rundir, strerror(errno));
		_exit(126);
	}
	for (i = 1; i < NSIG; i++)
		signal(i, SIG_DFL);
	env[0] = NULL;
	setenv_list(env, &n, 64, "HOME", rhome);
	setenv_list(env, &n, 64, "USER", C.user);
	setenv_list(env, &n, 64, "LOGNAME", C.user);
	setenv_list(env, &n, 64, "SHELL", rshell);
	setenv_list(env, &n, 64, "PATH", "/usr/local/bin:/usr/gnu/bin:/usr/bin:/usr/ucb:/usr/sbin");
	setenv_list(env, &n, 64, "TERM", term_name());
	setenv_list(env, &n, 64, "VSCODE_AGENT_DIR", sess);
	if ((e = getenv("TZ")) != NULL)
		setenv_list(env, &n, 64, "TZ", e);
	if ((e = getenv("LANG")) != NULL)
		setenv_list(env, &n, 64, "LANG", e);
	for (i = 0; i < nextra; i++) {
		char *eq = strchr(extra[i], '=');

		if (eq && eq != extra[i]) {
			*eq = 0;
			setenv_list(env, &n, 64, extra[i], eq + 1);
			if (strcmp(extra[i], "DISPLAY") == 0) {
				int has = 0, j;

				for (j = 0; j < nextra; j++)
					if (strncmp(extra[j], "XAUTHORITY=", 11) == 0)
						has = 1;
				if (!has) {
					snprintf(path, sizeof path, "%s/.Xauthority", rhome);
					setenv_list(env, &n, 64, "XAUTHORITY", path);
				}
			}
		}
	}
	/* a program that came with PUT runs from the session folder */
	if (strchr(argv[0], '/') == NULL) {
		snprintf(path, sizeof path, "%s/%s", sess, argv[0]);
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
			execve(path, argv, env);
			fprintf(stderr, "vscode-agent: %s: %s\r\n", path, strerror(errno));
			_exit(127);
		}
	}
	{
		extern char **environ;

		environ = env;
		execvp(argv[0], argv);
	}
	fprintf(stderr, "vscode-agent: %s: %s\r\n", argv[0], strerror(errno));
	_exit(127);
}

static void
cmd_run(char **t, int n)
{
	struct run *r = NULL;
	char *slavename, **extra, **argv;
	int i, cols, rows, nextra, dash = -1, m, s;
	pid_t p;

	if (n < 6 || !goodname(t[1], 31) || find_run(t[1])) {
		sendf("ERR RUN %s bad%%20request\n", n > 1 && goodname(t[1], 31) ? t[1] : "-");
		return;
	}
	for (i = 0; i < MAX_RUNS; i++)
		if (!runs[i].used) {
			r = &runs[i];
			break;
		}
	if (r == NULL) {
		sendf("ERR RUN %s too%%20many%%20programs%%20running\n", t[1]);
		return;
	}
	cols = atoi(t[2]);
	rows = atoi(t[3]);
	for (i = 4; i < n; i++) {
		if (strcmp(t[i], "--") == 0) {
			dash = i;
			break;
		}
		dec(t[i]);
	}
	if (dash < 0 || dash + 1 >= n) {
		sendf("ERR RUN %s no%%20program\n", t[1]);
		return;
	}
	extra = t + 4;
	nextra = dash - 4;
	argv = t + dash + 1;
	for (i = dash + 1; i < n; i++)
		dec(t[i]);
	t[n] = NULL;

	if ((m = open("/dev/ptmx", O_RDWR | O_NOCTTY)) < 0 || grantpt(m) != 0 || unlockpt(m) != 0 ||
	    (slavename = ptsname(m)) == NULL) {
		sendf("ERR RUN %s no%%20pty\n", t[1]);
		logmsg("pty: %s", strerror(errno));
		if (m >= 0)
			close(m);
		return;
	}
	if (is_root)
		chown(slavename, ruid, rgid);
	if ((s = open(slavename, O_RDWR | O_NOCTTY)) < 0) {
		sendf("ERR RUN %s no%%20pty\n", t[1]);
		close(m);
		return;
	}
#ifdef I_PUSH
	ioctl(s, I_PUSH, "ptem");
	ioctl(s, I_PUSH, "ldterm");
	ioctl(s, I_PUSH, "ttcompat");
#endif
	sane(s);
	set_size(s, cols > 0 ? cols : 80, rows > 0 ? rows : 24);
	if ((p = fork()) < 0) {
		sendf("ERR RUN %s fork%%20failed\n", t[1]);
		close(m);
		close(s);
		return;
	}
	if (p == 0) {
		close(m);
		close(s);
		close(sock);
		child(slavename, argv, extra, nextra);
	}
	fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
	memset(r, 0, sizeof *r);
	r->used = 1;
	snprintf(r->id, sizeof r->id, "%s", t[1]);
	r->pid = p;
	r->master = m;
	r->slave = s;
	logmsg("run %s: %s, pid %ld", r->id, argv[0], (long)p);
	sendf("STARTED %s %ld\n", r->id, (long)p);
}

static int
pump(struct run *r)
{
	char buf[4096];
	ssize_t n = read(r->master, buf, sizeof buf);

	if (n > 0) {
		sendf("OUT %s %ld\n", r->id, (long)n);
		sendall(buf, n);
	}
	return (int)n;
}

static void
finish_run(struct run *r)
{
	int i;

	/* what the program wrote before it ended */
	for (i = 0; i < 64 && pump(r) > 0; i++)
		;
	if (WIFSIGNALED(r->status))
		sendf("SIGNAL %s %d %d\n", r->id, WTERMSIG(r->status), WCOREDUMP(r->status) ? 1 : 0);
	else
		sendf("EXIT %s %d\n", r->id, WEXITSTATUS(r->status));
	logmsg("run %s ended, status 0x%x", r->id, r->status);
	close(r->master);
	close(r->slave);
	r->used = 0;
}

static void
reap(void)
{
	int st, i;
	pid_t p;

	while ((p = waitpid(-1, &st, WNOHANG)) > 0)
		for (i = 0; i < MAX_RUNS; i++)
			if (runs[i].used && runs[i].pid == p) {
				runs[i].exited = 1;
				runs[i].status = st;
			}
	for (i = 0; i < MAX_RUNS; i++)
		if (runs[i].used && runs[i].exited)
			finish_run(&runs[i]);
}

static void
kill_all(void)
{
	int i, left, k;

	for (i = 0; i < MAX_RUNS; i++)
		if (runs[i].used && !runs[i].exited) {
			kill(-runs[i].pid, SIGHUP);
			kill(runs[i].pid, SIGHUP);
		}
	for (k = 0; k < 20; k++) {
		int st;
		pid_t p;

		while ((p = waitpid(-1, &st, WNOHANG)) > 0)
			for (i = 0; i < MAX_RUNS; i++)
				if (runs[i].used && runs[i].pid == p) {
					runs[i].exited = 1;
					runs[i].status = st;
				}
		left = 0;
		for (i = 0; i < MAX_RUNS; i++)
			if (runs[i].used && !runs[i].exited)
				left++;
		if (!left)
			break;
		if (k == 10)
			for (i = 0; i < MAX_RUNS; i++)
				if (runs[i].used && !runs[i].exited) {
					kill(-runs[i].pid, SIGKILL);
					kill(runs[i].pid, SIGKILL);
				}
		poll(NULL, 0, 200);
	}
	for (i = 0; i < MAX_RUNS; i++)
		if (runs[i].used) {
			if (!runs[i].exited) {
				waitpid(runs[i].pid, NULL, 0);
				runs[i].status = SIGKILL;
			}
			if (!dead)
				finish_run(&runs[i]);
			else {
				close(runs[i].master);
				close(runs[i].slave);
				runs[i].used = 0;
			}
		}
}

/* --- files -------------------------------------------------------------- */

static void
cmd_put(char **t, int n)
{
	long size;
	char tmp[400];

	if (n != 5) {
		dead = 1;		/* the payload's length is unknown */
		return;
	}
	size = atol(t[3]);
	if (size < 0) {
		dead = 1;
		return;
	}
	need = size;
	sink = SINK_PUT;
	sinkfd = -1;
	sinkerr[0] = 0;
	snprintf(sinkname, sizeof sinkname, "%s", t[1]);
	snprintf(sinkwant, sizeof sinkwant, "%s", t[4]);
	sinkmode = (int)strtol(t[2], NULL, 8) & 0755;
	sha256_init(&sinksha);
	if (!goodname(t[1], 64) || strcmp(t[1], "run") == 0)
		snprintf(sinkerr, sizeof sinkerr, "bad name");
	else if (!ishex(t[4], 64, 64))
		snprintf(sinkerr, sizeof sinkerr, "bad sha256");
	else {
		long kb = free_kb(sess);

		if (kb >= 0 && kb - size / 1024 < C.reserve_kb)
			snprintf(sinkerr, sizeof sinkerr, "only %ld KB free in %s, %ld KB are kept free",
			    kb, C.tmpdir, C.reserve_kb);
		else {
			snprintf(tmp, sizeof tmp, "%s/.put.%s", sess, sinkname);
			unlink(tmp);
			if ((sinkfd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600)) < 0)
				snprintf(sinkerr, sizeof sinkerr, "%s", strerror(errno));
		}
	}
	/* an empty file is finished by the caller */
}

static void
put_done(void)
{
	uint8_t d[32];
	char got[65], tmp[400], dst[400], e[200];

	sha256_final(&sinksha, d);
	tohex(d, 32, got);
	snprintf(tmp, sizeof tmp, "%s/.put.%s", sess, sinkname);
	snprintf(dst, sizeof dst, "%s/%s", sess, sinkname);
	if (sinkfd >= 0) {
		if (close(sinkfd) != 0 && !sinkerr[0])
			snprintf(sinkerr, sizeof sinkerr, "%s", strerror(errno));
		sinkfd = -1;
		if (!sinkerr[0] && strcmp(got, sinkwant) != 0)
			snprintf(sinkerr, sizeof sinkerr, "sha256 mismatch");
		if (!sinkerr[0] && (chmod(tmp, sinkmode) != 0 || rename(tmp, dst) != 0))
			snprintf(sinkerr, sizeof sinkerr, "%s", strerror(errno));
		if (sinkerr[0])
			unlink(tmp);
	}
	if (sinkerr[0]) {
		enc(sinkerr, e, sizeof e);
		sendf("ERR PUT %s %s\n", sinkname, e);
		logmsg("put %s: %s", sinkname, sinkerr);
	} else
		sendf("OK PUT %s\n", sinkname);
}

/* GET: a child that is the user reads run/NAME and sends it itself */
static void
cmd_get(char **t, int n)
{
	pid_t p;
	int st;

	if (n != 2 || !goodname(t[1], 64)) {
		sendf("ERR GET - bad%%20name\n");
		return;
	}
	if ((p = fork()) == 0) {
		char path[400], buf[8192], e[200];
		struct stat sb;
		struct sha256 s;
		uint8_t d[32];
		char hex[65];
		ssize_t r;
		int fd;

		if (become_user() != 0)
			_exit(1);
		snprintf(path, sizeof path, "%s/%s", rundir, t[1]);
		if ((fd = open(path, O_RDONLY)) < 0 || fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
			enc(fd < 0 ? strerror(errno) : "not a file", e, sizeof e);
			sendf("ERR GET %s %s\n", t[1], e);
			_exit(0);
		}
		sha256_init(&s);
		while ((r = read(fd, buf, sizeof buf)) > 0)
			sha256_update(&s, buf, r);
		sha256_final(&s, d);
		tohex(d, 32, hex);
		lseek(fd, 0, SEEK_SET);
		sendf("FILE %s %ld %s\n", t[1], (long)sb.st_size, hex);
		{
			long left = (long)sb.st_size;

			while (left > 0 && (r = read(fd, buf, left < (long)sizeof buf ? left : (long)sizeof buf)) > 0) {
				sendall(buf, r);
				left -= r;
			}
			memset(buf, 0, sizeof buf);
			while (left > 0 && !dead) {	/* the file shrank: keep the frame whole */
				r = left < (long)sizeof buf ? left : (long)sizeof buf;
				sendall(buf, r);
				left -= r;
			}
		}
		_exit(0);
	}
	if (p < 0) {
		sendf("ERR GET %s fork%%20failed\n", t[1]);
		return;
	}
	while (waitpid(p, &st, 0) < 0 && errno == EINTR)
		;
	/* a run that ended meanwhile is reaped by the main loop */
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		sendf("ERR GET %s cannot%%20read%%20as%%20%s\n", t[1], C.user);
}

/* --- the commands ------------------------------------------------------- */

static void
hello(void)
{
	struct sha256 s;
	uint8_t d[32];
	static unsigned long count;
	struct tms tm;
	long v[6];

	/* stock AMIX has no /dev/urandom: the nonce only has to be new and
	 * unpredictable to someone without the secret */
	v[0] = (long)time(NULL);
	v[1] = (long)getpid();
	v[2] = (long)++count;
	v[3] = (long)clock();
	v[4] = (long)times(&tm);
	v[5] = (long)(size_t)&s;
	sha256_init(&s);
	sha256_update(&s, C.secret, strlen(C.secret));
	sha256_update(&s, nonce_a, strlen(nonce_a));
	sha256_update(&s, v, sizeof v);
	sha256_final(&s, d);
	tohex(d, 16, nonce_a);
	sendf("AMIX-AGENT %d %s %s\n", PROTO_VERSION, AGENT_VERSION, nonce_a);
}

static void
mac(const char *who, const char *first, const char *second, char out[65])
{
	char msg[200];
	uint8_t d[32];

	snprintf(msg, sizeof msg, "%s %s %s", who, first, second);
	hmac_sha256(C.secret, strlen(C.secret), msg, strlen(msg), d);
	tohex(d, 32, out);
}

static void
cmd_auth(char **t, int n)
{
	char want[65], reply[65], un[400], e[1200];
	struct utsname u;

	if (n != 3 || !ishex(t[1], 16, 64)) {
		dead = 1;
		return;
	}
	mac("ext", nonce_a, t[1], want);
	if (!same(want, t[2])) {
		logmsg("wrong key from the extension");
		sendf("ERR AUTH wrong%%20key\n");
		dead = 1;
		sleep(3);		/* slows down guessing */
		return;
	}
	if (make_session() != 0) {
		sendf("ERR AUTH no%%20session%%20folder\n");
		dead = 1;
		return;
	}
	authed = 1;
	mac("agent", t[1], nonce_a, reply);
	uname(&u);
	snprintf(un, sizeof un, "%.64s %.64s %.64s %.64s %.64s", u.sysname, u.nodename, u.release, u.version,
	    u.machine);
	enc(un, e, sizeof e);
	sendf("OK %s user=%s uid=%ld free=%ld dir=%s term=%s uname=%s\n", reply, C.user, (long)ruid,
	    free_kb(sess), sess, term_name(), e);
	logmsg("the extension signed in, session %s", sess);
}

static void
command(char *line)
{
	char *t[MAX_TOKENS + 1];
	int n = split(line, t, MAX_TOKENS);
	struct run *r;

	if (n == 0)
		return;
	if (!authed) {
		if (strcmp(t[0], "AUTH") == 0)
			cmd_auth(t, n);
		else
			dead = 1;
		return;
	}
	if (strcmp(t[0], "RUN") == 0)
		cmd_run(t, n);
	else if (strcmp(t[0], "IN") == 0 && n == 3) {
		need = atol(t[2]);
		sink = SINK_IN;
		r = find_run(t[1]);
		sinkrun = r ? (int)(r - runs) : -1;
		if (need < 0)
			dead = 1;
	} else if (strcmp(t[0], "PUT") == 0) {
		cmd_put(t, n);
		if (!dead && need == 0) {
			put_done();
			sink = SINK_NONE;
		}
	} else if (strcmp(t[0], "WINCH") == 0 && n == 4) {
		if ((r = find_run(t[1])) != NULL)
			set_size(r->slave, atoi(t[2]), atoi(t[3]));
	} else if (strcmp(t[0], "KILL") == 0 && n == 3) {
		if ((r = find_run(t[1])) != NULL && !r->exited) {
			int sig = atoi(t[2]);

			if (kill(-r->pid, sig) != 0)
				kill(r->pid, sig);
		}
	} else if (strcmp(t[0], "GET") == 0)
		cmd_get(t, n);
	else if (strcmp(t[0], "CLEAN") == 0) {
		kill_all();
		empty_rundir();
		remove_session();
		if (make_session() == 0)
			sendf("OK CLEAN %s\n", sess);
		else {
			sendf("ERR CLEAN no%%20session%%20folder\n");
			dead = 1;
		}
	} else if (strcmp(t[0], "PING") == 0)
		sendf("PONG %s\n", n > 1 ? t[1] : "-");
	else if (strcmp(t[0], "BYE") == 0)
		dead = 1;
	else
		sendf("ERR %s unknown%%20command\n", t[0]);
}

static void
feed(const char *p, size_t n)
{
	if (sink == SINK_PUT) {
		sha256_update(&sinksha, p, n);
		if (sinkfd >= 0 && !sinkerr[0] && write(sinkfd, p, n) != (ssize_t)n)
			snprintf(sinkerr, sizeof sinkerr, "%s", strerror(errno));
	} else if (sink == SINK_IN && sinkrun >= 0 && runs[sinkrun].used) {
		int fd = runs[sinkrun].master;

		while (n > 0) {
			ssize_t w = write(fd, p, n);

			if (w < 0 && errno == EAGAIN) {
				struct pollfd pf;

				pf.fd = fd;
				pf.events = POLLOUT;
				if (poll(&pf, 1, 1000) <= 0)
					break;
				continue;
			}
			if (w <= 0)
				break;
			p += w;
			n -= w;
		}
	}
}

static void
input(void)
{
	size_t pos = 0;

	while (pos < ilen && !dead) {
		if (need > 0) {
			size_t take = ilen - pos;

			if ((long)take > need)
				take = need;
			feed(ib + pos, take);
			pos += take;
			need -= take;
			if (need == 0) {
				if (sink == SINK_PUT)
					put_done();
				sink = SINK_NONE;
			}
			continue;
		}
		{
			char *nl = memchr(ib + pos, '\n', ilen - pos);

			if (nl == NULL) {
				if (ilen - pos > MAX_LINE)
					dead = 1;
				break;
			}
			*nl = 0;
			if (nl > ib + pos && nl[-1] == '\r')
				nl[-1] = 0;
			command(ib + pos);
			pos = nl + 1 - ib;
		}
	}
	memmove(ib, ib + pos, ilen - pos);
	ilen -= pos;
}

/* --- the connection ----------------------------------------------------- */

static void
on_alarm(int s)
{
	(void)s;
}

static void
on_term(int s)
{
	(void)s;
	stopping = 1;
}

static int
dial(const char *target)
{
	char host[256], *colon;
	struct sockaddr_in sa;
	struct hostent *he;
	int s, port;

	snprintf(host, sizeof host, "%s", target);
	if ((colon = strrchr(host, ':')) == NULL)
		return -1;
	*colon = 0;
	port = atoi(colon + 1);
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = (uint32_t)inet_addr(host);	/* AMIX: unsigned long, no in_addr_t */
	if (sa.sin_addr.s_addr == (uint32_t)-1) {
		if ((he = gethostbyname(host)) == NULL)
			return -1;
		memcpy(&sa.sin_addr, he->h_addr, sizeof sa.sin_addr);
	}
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;
	alarm(10);
	if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
		alarm(0);
		close(s);
		return -1;
	}
	alarm(0);
	return s;
}

static void
session(void)
{
	time_t start = time(NULL);

	dead = authed = 0;
	ilen = 0;
	need = 0;
	sink = SINK_NONE;
	hello();
	while (!dead && !stopping) {
		struct pollfd pf[1 + MAX_RUNS];
		int map[1 + MAX_RUNS], np = 1, i, rc;

		pf[0].fd = sock;
		pf[0].events = POLLIN;
		for (i = 0; i < MAX_RUNS; i++)
			if (runs[i].used && !runs[i].exited) {
				pf[np].fd = runs[i].master;
				pf[np].events = POLLIN;
				map[np++] = i;
			}
		rc = poll(pf, np, 250);
		if (rc < 0 && errno != EINTR)
			break;
		if (rc > 0 && (pf[0].revents & (POLLIN | POLLHUP | POLLERR))) {
			ssize_t r;

			if (icap - ilen < 16384) {
				char *nb = realloc(ib, icap + 32768);

				if (nb == NULL)
					break;
				ib = nb;
				icap += 32768;
			}
			r = read(sock, ib + ilen, icap - ilen);
			if (r <= 0) {
				if (r < 0 && errno == EINTR)
					continue;
				break;
			}
			ilen += r;
			input();
		}
		for (i = 1; rc > 0 && i < np; i++)
			if ((pf[i].revents & (POLLIN | POLLHUP | POLLERR)) && runs[map[i]].used)
				pump(&runs[map[i]]);
		reap();
		if (!authed && time(NULL) - start > AUTH_SECONDS) {
			logmsg("no sign-in within %d s", AUTH_SECONDS);
			break;
		}
	}
	dead = 1;
	kill_all();
	if (sinkfd >= 0) {
		close(sinkfd);
		sinkfd = -1;
	}
	remove_session();
	authed = 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: vscode-agent [-f] [-c conf]\n"
	    "  -f  stay in the foreground, log to stderr\n"
	    "  -c  configuration (default " CONF_DEFAULT ")\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	const char *conf = CONF_DEFAULT;
	struct sigaction sa;
	int i, failed[MAX_CONNECT];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0)
			foreground = 1;
		else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
			conf = argv[++i];
		else if (strcmp(argv[i], "-V") == 0) {
			printf("vscode-agent %s, protocol %d\n", AGENT_VERSION, PROTO_VERSION);
			return 0;
		} else
			usage();
	}
	is_root = geteuid() == 0;
	if (read_conf(conf) != 0)
		return 1;

	if (!foreground) {
		pid_t p = fork();

		if (p < 0)
			return 1;
		if (p > 0)
			return 0;
		setsid();
		chdir("/");
		i = open("/dev/null", O_RDWR);
		dup2(i, 0);
		dup2(i, 1);
		dup2(i, 2);
		if (i > 2)
			close(i);
		if (C.log[0]) {
			struct stat st;

			if (stat(C.log, &st) == 0 && st.st_size > 64L * 1024) {
				char old[300];

				snprintf(old, sizeof old, "%s.old", C.log);
				rename(C.log, old);
			}
			logf = fopen(C.log, "a");
		}
		if (is_root) {
			FILE *f = fopen(PID_FILE, "w");

			if (f) {
				fprintf(f, "%ld\n", (long)getpid());
				fclose(f);
			}
		}
	}
	umask(022);
#ifdef __amix__
	/* AMIX's user licensing kills a non-root process with a terminal at
	 * its first fork unless it descends from a registered login; only
	 * root may register, and the programs inherit it from here */
	if (is_root && sysm68k(_m68k_LIMUSER, EUA_ADD_USER) == -1)
		logmsg("sysm68k EUA_ADD_USER: %s", strerror(errno));
#endif
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sa.sa_handler = on_alarm;
	sigaction(SIGALRM, &sa, NULL);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	logmsg("%s started, runs programs as %s", AGENT_VERSION, C.user);
	memset(failed, 0, sizeof failed);
	while (!stopping) {
		int had = 0;

		for (i = 0; i < C.nconnect && !stopping; i++) {
			if ((sock = dial(C.connect[i])) < 0) {
				if (!failed[i])
					logmsg("%s: no listener (quiet until it answers)", C.connect[i]);
				failed[i] = 1;
				continue;
			}
			failed[i] = 0;
			logmsg("connected to %s", C.connect[i]);
			session();
			had = 1;
			close(sock);
			sock = -1;
			logmsg("%s: connection closed", C.connect[i]);
			break;
		}
		/* back soon after a session: the editor may just have reloaded */
		if (!stopping)
			sleep(had ? 1 : C.retry);
	}
	logmsg("stopped");
	if (is_root && !foreground)
		unlink(PID_FILE);
	return 0;
}
