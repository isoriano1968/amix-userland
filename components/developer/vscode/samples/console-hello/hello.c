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
#include <signal.h>
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

/*
 * A resized terminal (VS Code's panel, an xterm): ncurses reports it as
 * KEY_RESIZE.  SVR4 curses keeps the size it started with and would draw over
 * the wrong lines, so on SIGWINCH the program makes a new screen, which reads
 * the new size from the terminal.
 */
#ifndef KEY_RESIZE
static volatile sig_atomic_t resized;

static void on_winch(int sig)
{
    resized = 1;
    signal(sig, on_winch);      /* SVR4 signal() resets the handler */
}
#endif

static SCREEN *scr;

static WINDOW *start_screen(void)
{
    WINDOW *w;
    int rows, cols;

    scr = newterm(NULL, stdout, stdin);
    set_term(scr);
    cbreak();
    noecho();
    curs_set(0);
    getmaxyx(stdscr, rows, cols);
    refresh();

    w = newwin(12, 50, rows > 12 ? (rows - 12) / 2 : 0, cols > 50 ? (cols - 50) / 2 : 0);
    keypad(w, TRUE);
    halfdelay(10);              /* wake up every second for the clock */
    return w;
}

static WINDOW *restart_screen(WINDOW *w)
{
    SCREEN *old = scr;

    delwin(w);
    endwin();
    w = start_screen();
    delscreen(old);
    clearok(curscr, TRUE);      /* the whole terminal again, not a diff */
    return w;
}

int main(void)
{
    struct utsname u;
    WINDOW *w;
    int sel = 0, ch;
    char msg[64] = "";

    uname(&u);
    w = start_screen();
#ifndef KEY_RESIZE
    signal(SIGWINCH, on_winch);
#endif

    for (;;) {
        draw(w, &u, sel, msg);
        ch = wgetch(w);
#ifdef KEY_RESIZE
        if (ch == KEY_RESIZE) {
            w = restart_screen(w);
            continue;
        }
#else
        if (resized) {
            resized = 0;
            w = restart_screen(w);
            continue;
        }
#endif
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
