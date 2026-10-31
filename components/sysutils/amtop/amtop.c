/*
 * amtop - small curses process/system monitor for AMIX/SVR4.
 *
 * This program avoids ncurses-only interfaces and kernel-private structures.
 * It gathers data from ps and swap so it can run on old installations where
 * a modern /proc layout or libkvm is not available.
 */

#include <curses.h>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_LINES 256
#define MAX_LINE 160
#define REFRESH_SECS 5

struct text_block {
	char line[MAX_LINES][MAX_LINE];
	int count;
};

struct load_info {
	int total;
	int runnable;
	int sleeping;
	int stopped;
	int zombies;
};

static int done;
static char status_msg[MAX_LINE];

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

static int read_command(cmd, out)
char *cmd;
struct text_block *out;
{
	FILE *fp;
	char buf[MAX_LINE];

	out->count = 0;
	fp = popen(cmd, "r");
	if (fp == NULL) {
		sprintf(status_msg, "cannot run: %.120s", cmd);
		return -1;
	}

	while (out->count < MAX_LINES && fgets(buf, sizeof(buf), fp) != NULL) {
		trim_newline(buf);
		strncpy(out->line[out->count], buf, MAX_LINE - 1);
		out->line[out->count][MAX_LINE - 1] = '\0';
		out->count++;
	}

	if (pclose(fp) == -1) {
		sprintf(status_msg, "error closing: %.118s", cmd);
		return -1;
	}

	return out->count;
}

static int is_blank(s)
char *s;
{
	while (*s) {
		if (!isspace((unsigned char)*s))
			return 0;
		s++;
	}
	return 1;
}

static int starts_with_digit(s)
char *s;
{
	while (*s == ' ' || *s == '\t')
		s++;
	return isdigit((unsigned char)*s);
}

static int gather_processes(ps)
struct text_block *ps;
{
	if (read_command("ps -el 2>/dev/null", ps) > 1)
		return ps->count;
	return read_command("ps -ef 2>/dev/null", ps);
}

static int gather_swap(summary, list)
struct text_block *summary;
struct text_block *list;
{
	int n;

	n = read_command("swap -s 2>/dev/null", summary);
	read_command("swap -l 2>/dev/null", list);
	return n;
}

static int ps_state(line)
char *line;
{
	char *p;

	p = line;
	while (*p == ' ' || *p == '\t')
		p++;
	while (*p && *p != ' ' && *p != '\t')
		p++;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '\0')
		return 0;
	return *p;
}

static void gather_load(ps, load)
struct text_block *ps;
struct load_info *load;
{
	int i, st;

	memset((char *)load, 0, sizeof(*load));
	for (i = 0; i < ps->count; i++) {
		if (!starts_with_digit(ps->line[i]))
			continue;
		load->total++;
		st = ps_state(ps->line[i]);
		if (st == 'R' || st == 'O')
			load->runnable++;
		else if (st == 'S')
			load->sleeping++;
		else if (st == 'T')
			load->stopped++;
		else if (st == 'Z')
			load->zombies++;
	}
	if (load->total == 0 && ps->count > 1)
		load->total = ps->count - 1;
}

static int process_count(ps)
struct text_block *ps;
{
	int i, n;

	n = 0;
	for (i = 0; i < ps->count; i++) {
		if (starts_with_digit(ps->line[i]))
			n++;
	}
	if (n == 0 && ps->count > 1)
		n = ps->count - 1;
	return n;
}

static void draw_header(procs)
int procs;
{
	time_t now;
	char *t;

	now = time((time_t *)0);
	t = ctime(&now);
	trim_newline(t);

	attron(A_REVERSE);
	move(0, 0);
	clrtoeol();
	mvprintw(0, 0, "amtop  %s  processes: %d  refresh: %ds  q: quit",
	    t, procs, REFRESH_SECS);
	attroff(A_REVERSE);
}

static int draw_processes(row, ps)
int row;
struct text_block *ps;
{
	int i, shown, max_rows;

	attron(A_BOLD);
	mvprintw(row, 0, "Processes");
	attroff(A_BOLD);

	max_rows = LINES - row - 2;
	shown = 0;
	for (i = 0; i < ps->count && shown < max_rows; i++) {
		if (is_blank(ps->line[i]))
			continue;
		mvprintw(row + 1 + shown, 0, "%.*s", COLS - 1, ps->line[i]);
		shown++;
	}
	return shown;
}

static void draw_screen(ps, swap_summary, swap_list)
struct text_block *ps;
struct text_block *swap_summary;
struct text_block *swap_list;
{
	struct load_info load;
	int row;

	erase();
	gather_load(ps, &load);
	draw_header(load.total ? load.total : process_count(ps));

	row = 2;
	attron(A_BOLD);
	mvprintw(row, 0, "Load");
	attroff(A_BOLD);
	mvprintw(row + 1, 0,
	    "runnable/on-cpu: %d  sleeping: %d  stopped: %d  zombies: %d",
	    load.runnable, load.sleeping, load.stopped, load.zombies);

	row += 3;
	attron(A_BOLD);
	mvprintw(row, 0, "Memory / Swap");
	attroff(A_BOLD);
	if (swap_summary->count > 0)
		mvprintw(row + 1, 0, "%.*s", COLS - 1, swap_summary->line[0]);
	else
		mvprintw(row + 1, 0, "swap -s unavailable");
	if (swap_list->count > 0)
		mvprintw(row + 2, 0, "%.*s", COLS - 1, swap_list->line[0]);
	if (swap_list->count > 1)
		mvprintw(row + 3, 0, "%.*s", COLS - 1, swap_list->line[1]);

	row += 6;
	draw_processes(row, ps);

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
	struct text_block ps, swap_summary, swap_list;

	(void)argc;
	(void)argv;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	initscr();
	cbreak();
	noecho();
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);

	while (!done) {
		status_msg[0] = '\0';
		gather_processes(&ps);
		gather_swap(&swap_summary, &swap_list);
		draw_screen(&ps, &swap_summary, &swap_list);
		if (wait_key(REFRESH_SECS))
			break;
	}

	endwin();
	return 0;
}
