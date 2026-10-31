/*
 * amtop - small curses process/system monitor for AMIX/SVR4.
 *
 * It gathers data from ps, swap and ztop, so it runs where a modern /proc
 * layout or libkvm is not available.  Colours when the terminal has them
 * (dtterm does; the entry for the amiga console has none): the header, the
 * sections, each process by its state, and the bar for core 1 of a Z3660.
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
#define REFRESH_SECS 2

enum { C_HEADER = 1, C_TITLE, C_RUN, C_STOP, C_ZOMBIE, C_BAR, C_BARHIGH };

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
static int colour;

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

static void paint(pair, bold)
int pair, bold;
{
	if (colour)
		attrset(COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
	else
		attrset(bold ? A_BOLD : A_NORMAL);
}

/* a bar of width cells, per-mille full */
static void draw_bar(row, col, width, permille)
int row, col, width, permille;
{
	int i, n;

	n = permille * width / 1000;
	mvaddch(row, col, '[');
	for (i = 0; i < width; i++) {
		paint(permille > 850 ? C_BARHIGH : C_BAR, 1);
		if (colour)
			addch(i < n ? (' ' | A_REVERSE) : ' ');
		else
			addch(i < n ? '#' : ' ');
	}
	attrset(A_NORMAL);
	addch(']');
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
	mvprintw(0, 0, "amtop  %s  processes: %d  refresh: %ds  q: quit",
	    t, procs, REFRESH_SECS);
	attrset(A_NORMAL);
}

static int draw_processes(row, ps)
int row;
struct text_block *ps;
{
	int i, shown, max_rows;

	paint(C_TITLE, 1);
	mvprintw(row, 0, "Processes");
	attrset(A_NORMAL);

	max_rows = LINES - row - 2;
	shown = 0;
	for (i = 0; i < ps->count && shown < max_rows; i++) {
		if (is_blank(ps->line[i]))
			continue;
		switch (starts_with_digit(ps->line[i]) ? ps_state(ps->line[i]) : 0) {
		case 'R': case 'O':
			paint(C_RUN, 1);
			break;
		case 'T':
			paint(C_STOP, 0);
			break;
		case 'Z':
			paint(C_ZOMBIE, 1);
			break;
		case 0:
			paint(C_TITLE, 0);
			break;
		default:
			attrset(A_NORMAL);
		}
		mvprintw(row + 1 + shown, 0, "%.*s", COLS - 1, ps->line[i]);
		attrset(A_NORMAL);
		shown++;
	}
	return shown;
}

/* Z3660: "ARM core 1      gpu, decoding, load 81.3 %, 866 MHz" */
static void draw_core1(row, core)
int row;
struct text_block *core;
{
	char *p;
	int whole, tenth;

	if (core->count < 1 || (p = strstr(core->line[0], "load ")) == NULL ||
	    sscanf(p, "load %d.%d", &whole, &tenth) != 2)
		return;
	paint(C_TITLE, 1);
	mvprintw(row, 0, "Core 1");
	attrset(A_NORMAL);
	draw_bar(row, 8, 30, whole * 10 + tenth);
	mvprintw(row, 41, "%.*s", COLS - 42, core->line[0] + 16);
}

static void draw_screen(ps, swap_summary, swap_list, core)
struct text_block *ps;
struct text_block *swap_summary;
struct text_block *swap_list;
struct text_block *core;
{
	struct load_info load;
	int row;

	erase();
	gather_load(ps, &load);
	draw_header(load.total ? load.total : process_count(ps));

	row = 2;
	paint(C_TITLE, 1);
	mvprintw(row, 0, "Load");
	attrset(A_NORMAL);
	paint(C_RUN, 1);
	mvprintw(row + 1, 0, "running: %d", load.runnable);
	attrset(A_NORMAL);
	printw("  sleeping: %d  ", load.sleeping);
	paint(C_STOP, 0);
	printw("stopped: %d  ", load.stopped);
	paint(C_ZOMBIE, load.zombies > 0);
	printw("zombies: %d", load.zombies);
	attrset(A_NORMAL);

	row += 3;
	paint(C_TITLE, 1);
	mvprintw(row, 0, "Memory / Swap");
	attrset(A_NORMAL);
	if (swap_summary->count > 0)
		mvprintw(row + 1, 0, "%.*s", COLS - 1, swap_summary->line[0]);
	else
		mvprintw(row + 1, 0, "swap -s unavailable");
	if (swap_list->count > 0)
		mvprintw(row + 2, 0, "%.*s", COLS - 1, swap_list->line[0]);
	if (swap_list->count > 1)
		mvprintw(row + 3, 0, "%.*s", COLS - 1, swap_list->line[1]);

	row += 5;
	draw_core1(row, core);
	row += 2;
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
	struct text_block ps, swap_summary, swap_list, core;

	(void)argc;
	(void)argv;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

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
		colour = 1;
	}
	curs_set(0);
	cbreak();
	noecho();
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);

	while (!done) {
		status_msg[0] = '\0';
		gather_processes(&ps);
		gather_swap(&swap_summary, &swap_list);
		read_command("/usr/amiga/bin/ztop 2>/dev/null | grep \"ARM core 1\"", &core);
		draw_screen(&ps, &swap_summary, &swap_list, &core);
		if (wait_key(REFRESH_SECS))
			break;
	}

	endwin();
	return 0;
}
