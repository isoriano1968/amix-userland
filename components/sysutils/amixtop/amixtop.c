/*
 * amixtop - small curses process/system monitor for AMIX/SVR4.
 *
 * Its numbers come from libamixstat (library/amixstat): the CPU's user and
 * system shares (from the kernel profiler: AMIX books idle ticks as system
 * time of the last process), memory, swap, the network interface, and the
 * processes from /proc, sorted by their CPU share.  Core 1 of a Z3660 comes
 * from ztop.  amixtop is installed setgid sys, as the library reads /dev/kmem.
 * Colours when the terminal has them (dtterm does; the entry for the amiga
 * console has none): the header, the sections, each process by its state,
 * and the bars.
 */

#include <curses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <amixstat.h>

#define MAX_LINE 160
#define REFRESH_SECS 2

enum { C_HEADER = 1, C_TITLE, C_RUN, C_STOP, C_ZOMBIE, C_BAR, C_BARHIGH, C_SYS };

struct row {
	struct ast_proc p;
	int pcpu;		/* per mille of the last interval */
};

static int done;
static char status_msg[MAX_LINE];
static int colour;
static int opened;

static struct ast_cpu cpu0;
static int have_cpu0;
static int user_pm, sys_pm;	/* the CPU's shares of the last interval, per mille */
static int prf_ok;

static struct ast_proc *prev;
static int nprev;
static unsigned long prev_ticks;
static struct row *rows;
static int nrows;

static unsigned long net_in0, net_out0;
static int have_net0;
static char netname[16];
static long net_in, net_out;	/* packets a second */

static void on_signal(sig)
int sig;
{
	(void)sig;
	done = 1;
}

static void trim_newline(s)
char *s;
{
	int n;

	n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
		s[--n] = '\0';
	}
}

static char *user_name(uid)
uid_t uid;
{
	static uid_t cu[64];
	static char cn[64][9];
	static int n;
	struct passwd *pw;
	int i;

	for (i = 0; i < n; i++)
		if (cu[i] == uid)
			return cn[i];
	if (n == 64)
		n = 0;
	cu[n] = uid;
	if ((pw = getpwuid(uid)) != NULL)
		sprintf(cn[n], "%.8s", pw->pw_name);
	else
		sprintf(cn[n], "%ld", (long)uid);
	return cn[n++];
}

/* KB for people */
static void human(buf, kb)
char *buf;
unsigned long kb;
{
	if (kb >= 1048576UL)
		sprintf(buf, "%.1fG", kb / 1048576.0);
	else if (kb >= 10240UL)
		sprintf(buf, "%luM", (kb + 512) / 1024);
	else
		sprintf(buf, "%luK", kb);
}

static int cmp_rows(a, b)
const void *a, *b;
{
	const struct row *x = a, *y = b;

	if (x->pcpu != y->pcpu)
		return y->pcpu - x->pcpu;
	return x->p.rss < y->p.rss ? 1 : x->p.rss > y->p.rss ? -1 : 0;
}

/* the CPU, the network, and the processes' shares since the last time */
static void gather()
{
	struct ast_cpu c;
	struct ast_proc *cur;
	struct ast_if *ifs;
	int n, i, j, ni;
	unsigned long ssum = 0, dt;
	long k1000 = 1000;	/* system time kept, per mille */

	if (ast_cpu(&c) == 0 && c.prf) {
		if (have_cpu0) {
			unsigned long u = c.s_user - cpu0.s_user, k = c.s_kernel - cpu0.s_kernel;
			unsigned long all = u + k + (c.s_idle - cpu0.s_idle);

			if (all > 0) {
				user_pm = (int)(1000 * u / all);
				sys_pm = (int)(1000 * k / all);
			}
		}
		cpu0 = c;
		have_cpu0 = 1;
		prf_ok = 1;
	} else
		prf_ok = 0;

	netname[0] = 0;
	if ((ni = ast_ifs(&ifs)) > 0) {
		for (i = 0; i < ni && strncmp(ifs[i].name, "lo", 2) == 0; i++)
			;
		if (i < ni) {
			strcpy(netname, ifs[i].name);
			net_in = have_net0 ? (long)(ifs[i].ipackets - net_in0) / REFRESH_SECS : 0;
			net_out = have_net0 ? (long)(ifs[i].opackets - net_out0) / REFRESH_SECS : 0;
			net_in0 = ifs[i].ipackets;
			net_out0 = ifs[i].opackets;
			have_net0 = 1;
		}
	}
	free(ifs);

	if ((n = ast_procs(&cur)) < 0) {
		sprintf(status_msg, "cannot read /proc");
		return;
	}
	/* AMIX books idle ticks as system time: scale the processes' system
	   time to the kernel's real share */
	dt = c.hz ? (c.lbolt - prev_ticks) * 1000 / c.hz : 0;	/* ms */
	for (i = 0; i < n; i++) {
		for (j = 0; j < nprev && prev[j].pid != cur[i].pid; j++)
			;
		if (j < nprev && cur[i].stime > prev[j].stime)
			ssum += cur[i].stime - prev[j].stime;
	}
	if (prf_ok && ssum > 0 && dt > 0 && (unsigned long)sys_pm * dt / 1000 < ssum)
		k1000 = (long)((unsigned long)sys_pm * dt / ssum);
	rows = realloc(rows, (n + 1) * sizeof *rows);
	for (i = 0; i < n; i++) {
		long d = 0;

		for (j = 0; j < nprev && prev[j].pid != cur[i].pid; j++)
			;
		if (j < nprev && dt > 0) {
			long du = (long)(cur[i].utime - prev[j].utime);
			long ds = (long)(cur[i].stime - prev[j].stime);

			d = ((du > 0 ? du : 0) + (ds > 0 ? ds * k1000 / 1000 : 0)) * 1000 / (long)dt;
		}
		rows[i].p = cur[i];
		rows[i].pcpu = d > 1000 ? 1000 : (int)d;
	}
	nrows = n;
	free(prev);
	prev = cur;
	nprev = n;
	prev_ticks = c.lbolt;
	qsort(rows, nrows, sizeof *rows, cmp_rows);
}

static void paint(pair, bold)
int pair, bold;
{
	if (colour)
		attrset(COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
	else
		attrset(bold ? A_BOLD : A_NORMAL);
}

/* a bar of width cells: a per mille in the bar colour, b after it in the
   system colour */
static void draw_bar(row, col, width, a, b)
int row, col, width, a, b;
{
	int i, na, nb;

	na = a * width / 1000;
	nb = (a + b) * width / 1000;
	mvaddch(row, col, '[');
	for (i = 0; i < width; i++) {
		if (i < na)
			paint(a + b > 850 ? C_BARHIGH : C_BAR, 1);
		else if (i < nb)
			paint(C_SYS, 1);
		else
			attrset(A_NORMAL);
		if (colour)
			addch(i < nb ? (' ' | A_REVERSE) : ' ');
		else
			addch(i < na ? '#' : i < nb ? '=' : ' ');
	}
	attrset(A_NORMAL);
	addch(']');
}

static void label(row, s)
int row;
char *s;
{
	paint(C_TITLE, 1);
	mvprintw(row, 0, "%s", s);
	attrset(A_NORMAL);
}

static void draw_header(procs)
int procs;
{
	time_t now;
	char *t;

	now = time((time_t *)0);
	t = ctime(&now);
	trim_newline(t);

	if (colour)
		attrset(COLOR_PAIR(C_HEADER) | A_BOLD);
	else
		attrset(A_REVERSE);
	mvhline(0, 0, ' ', COLS);
	mvprintw(0, 0, "amixtop  %s  processes: %d  refresh: %ds  q: quit",
	    t, procs, REFRESH_SECS);
	attrset(A_NORMAL);
}

/* Z3660: "ARM core 1      gpu, decoding, load 81.3 %, 866 MHz" */
static int draw_core1(row)
int row;
{
	char line[MAX_LINE], *p;
	int whole, tenth, ok = 0;
	FILE *fp;

	if ((fp = popen("/usr/amiga/bin/ztop 2>/dev/null | grep \"ARM core 1\"", "r")) == NULL)
		return 0;
	if (fgets(line, sizeof line, fp) != NULL) {
		trim_newline(line);
		if ((p = strstr(line, "load ")) != NULL && sscanf(p, "load %d.%d", &whole, &tenth) == 2 &&
		    strlen(line) > 16) {
			label(row, "Core 1");
			draw_bar(row, 8, 30, whole * 10 + tenth, 0);
			mvprintw(row, 41, "%.*s", COLS - 42, line + 16);
			ok = 1;
		}
	}
	pclose(fp);
	return ok;
}

static void draw_processes(row)
int row;
{
	int i, shown, max_rows;
	char rss[16], vsz[16];

	paint(C_TITLE, 1);
	mvprintw(row, 0, "%6s %-8s S  CPU%% %6s %6s %8s  %s", "PID", "USER", "RES", "VIRT", "TIME", "COMMAND");
	attrset(A_NORMAL);
	max_rows = LINES - row - 2;
	shown = 0;
	for (i = 0; i < nrows && shown < max_rows; i++) {
		struct ast_proc *p = &rows[i].p;
		long t = (long)(p->cpu / 1000);

		switch (p->state) {
		case 'R': case 'O':
			paint(C_RUN, 1);
			break;
		case 'T':
			paint(C_STOP, 0);
			break;
		case 'Z':
			paint(C_ZOMBIE, 1);
			break;
		default:
			attrset(A_NORMAL);
		}
		human(rss, p->rss);
		human(vsz, p->size);
		mvprintw(row + 1 + shown, 0, "%6ld %-8s %c %5.1f %6s %6s %5ld:%02ld  %.*s", (long)p->pid,
		    user_name(p->uid), p->state, rows[i].pcpu / 10.0, rss, vsz, t / 60, t % 60,
		    COLS > 58 ? COLS - 58 : 1, p->args[0] ? p->args : p->fname);
		attrset(A_NORMAL);
		shown++;
	}
}

static void draw_screen()
{
	struct ast_mem m;
	int row, i, run = 0, slp = 0, stp = 0, zom = 0;
	char a[16], b[16];

	erase();
	for (i = 0; i < nrows; i++) {
		switch (rows[i].p.state) {
		case 'R': case 'O': run++; break;
		case 'S': slp++; break;
		case 'T': stp++; break;
		case 'Z': zom++; break;
		}
	}
	draw_header(nrows);

	row = 2;
	label(row, "CPU");
	if (prf_ok) {
		draw_bar(row, 8, 30, user_pm, sys_pm);
		printw(" user %4.1f %%  system %4.1f %%  idle %4.1f %%", user_pm / 10.0, sys_pm / 10.0,
		    (1000 - user_pm - sys_pm) / 10.0);
	} else
		mvprintw(row, 8, "unknown: the kernel profiler (/dev/prf) is in use");

	if (opened && ast_mem(&m) == 0) {
		label(row + 1, "Memory");
		draw_bar(row + 1, 8, 30, m.total ? (int)(1000.0 * (m.total - m.free) / m.total) : 0, 0);
		human(a, m.total - m.free);
		human(b, m.total);
		printw(" %s of %s", a, b);
		label(row + 2, "Swap");
		draw_bar(row + 2, 8, 30, m.swap_total ? (int)(1000.0 * (m.swap_total - m.swap_free) / m.swap_total) : 0, 0);
		human(a, m.swap_total - m.swap_free);
		human(b, m.swap_total);
		printw(" %s of %s", a, b);
		human(a, m.swap_resv);
		printw(", %s reserved", a);
	} else
		mvprintw(row + 1, 0, "Memory and swap: unknown (amixtop needs group sys for /dev/kmem)");

	label(row + 3, "Net");
	if (netname[0])
		mvprintw(row + 3, 8, "%-6s in %ld packets/s  out %ld packets/s", netname, net_in, net_out);

	label(row + 4, "Tasks");
	paint(C_RUN, 1);
	mvprintw(row + 4, 8, "running: %d", run);
	attrset(A_NORMAL);
	printw("  sleeping: %d  ", slp);
	paint(C_STOP, 0);
	printw("stopped: %d  ", stp);
	paint(C_ZOMBIE, zom > 0);
	printw("zombies: %d", zom);
	attrset(A_NORMAL);

	row += 5;
	if (draw_core1(row))
		row++;
	draw_processes(row + 1);

	if (status_msg[0]) {
		attron(A_REVERSE);
		mvprintw(LINES - 1, 0, "%.*s", COLS - 1, status_msg);
		attroff(A_REVERSE);
	}

	refresh();
}

static int wait_key(seconds)
int seconds;
{
	int i, ch;

	for (i = 0; i < seconds && !done; i++) {
		ch = getch();
		if (ch == 'q' || ch == 'Q')
			return 1;
		if (ch == '\014') {
			clearok(stdscr, TRUE);
			refresh();
		}
		sleep(1);
	}
	return 0;
}

int main(argc, argv)
int argc;
char **argv;
{
	char err[MAX_LINE];

	(void)argc;
	(void)argv;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	opened = ast_open(err, sizeof err) == 0;

	initscr();
	if (has_colors()) {
		short back;

		start_color();
		back = use_default_colors() == OK ? -1 : COLOR_BLACK;
		init_pair(C_HEADER, COLOR_WHITE, COLOR_BLUE);
		init_pair(C_TITLE, COLOR_CYAN, back);
		init_pair(C_RUN, COLOR_GREEN, back);
		init_pair(C_STOP, COLOR_YELLOW, back);
		init_pair(C_ZOMBIE, COLOR_RED, back);
		init_pair(C_BAR, COLOR_GREEN, back);
		init_pair(C_BARHIGH, COLOR_RED, back);
		init_pair(C_SYS, COLOR_YELLOW, back);
		colour = 1;
	}
	curs_set(0);
	cbreak();
	noecho();
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);

	gather();	/* the first interval starts here: no CPU shares yet */
	draw_screen();
	while (!done) {
		status_msg[0] = '\0';
		if (!opened)
			sprintf(status_msg, "%.150s", err);
		if (wait_key(REFRESH_SECS))
			break;
		gather();
		draw_screen();
	}

	endwin();
	return 0;
}
