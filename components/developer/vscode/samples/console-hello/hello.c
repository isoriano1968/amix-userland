/*
 * hello.c -- a native AMIX console program with curses: a framed window with
 * the system name, a clock and a small menu (arrow keys, Enter, q to quit).
 *
 *   make                    gcc 14 (m68k-elf-gcc), the default
 *   make TOOLCHAIN=gcc272   gcc 2.7.2.3 (m68k-cbm-sysv4-gcc)
 *
 * On the A3000, in a console, dtterm or ssh login: ./hello (on the console
 * TERM is already amiga; elsewhere any terminfo entry the ncurses package
 * installs: vt100, xterm, dtterm).
 * Only classic curses calls, as in SVR4 curses and ncurses.
 */
#include <curses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>

static const char *items[] = { "Say hello", "Show the time", "Quit" };
#define NITEMS (sizeof items / sizeof items[0])

static void draw(WINDOW *w, struct utsname *u, int sel, const char *msg)
{
    char now[64], line[3 * sizeof u->sysname + 8];
    time_t t = time(NULL);
    int i;

    strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", localtime(&t));
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " Hello AMIX ");
    /* AMIX says "UNIX_System_V 4.0 on amigaunix (Amiga (Unlimited))", wider
     * than the window: system on one row, machine on the next, each cut to
     * the 44 columns inside the frame, or it wraps over the border */
    sprintf(line, "%s %s on %s", u->sysname, u->release, u->nodename);
    mvwprintw(w, 2, 3, "%-44.44s", line);
    mvwprintw(w, 3, 3, "%-44.44s", u->machine);
    mvwprintw(w, 4, 3, "%s", now);
    for (i = 0; i < (int)NITEMS; i++) {
        if (i == sel)
            wattron(w, A_REVERSE);
        mvwprintw(w, 6 + i, 5, " %-16s ", items[i]);
        if (i == sel)
            wattroff(w, A_REVERSE);
    }
    mvwprintw(w, 9, 3, "%-40.40s", msg);
    mvwprintw(w, 10, 3, "arrows: move  Enter: choose  q: quit");
    wrefresh(w);
}

int main(void)
{
    struct utsname u;
    WINDOW *w;
    int sel = 0, ch, rows, cols;
    char msg[64] = "";

    uname(&u);
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    getmaxyx(stdscr, rows, cols);
    refresh();

    w = newwin(12, 50, (rows - 12) / 2, (cols - 50) / 2);
    keypad(w, TRUE);
    halfdelay(10);              /* wake up every second for the clock */

    for (;;) {
        draw(w, &u, sel, msg);
        ch = wgetch(w);
        if (ch == 'q' || ch == 'Q')
            break;
        if (ch == KEY_UP || ch == 'k')
            sel = (sel + NITEMS - 1) % NITEMS;
        else if (ch == KEY_DOWN || ch == 'j')
            sel = (sel + 1) % NITEMS;
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (sel == 0)
                strcpy(msg, "Hello, world!");
            else if (sel == 1) {
                time_t t = time(NULL);
                strftime(msg, sizeof msg, "It is %H:%M.", localtime(&t));
            } else
                break;
        }
    }

    delwin(w);
    endwin();
    return 0;
}
