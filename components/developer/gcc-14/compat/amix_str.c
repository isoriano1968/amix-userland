/* amix_str.c -- the case-insensitive string comparisons AMIX does not have.
 *
 * strcasecmp and strncasecmp came from BSD and reached POSIX in 2001; SVR4.0
 * has neither, in libc.a or in libc.so -- checked by linking, not by reading a
 * header, because AMIX's headers and its library disagree often enough that
 * only the linker settles it.
 *
 * ASCII only, deliberately: tolower() in a C locale is what every caller here
 * means, and going through <ctype.h> would make the result depend on a locale
 * this target barely has.
 */
#include <stddef.h>

#include "amix_compat.h"

static int amix_lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int strcasecmp(const char *a, const char *b)
{
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;
	int d;

	for (;;) {
		d = amix_lower(*p) - amix_lower(*q);
		if (d != 0 || *p == '\0') return d;
		p++; q++;
	}
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;
	int d;

	while (n-- > 0) {
		d = amix_lower(*p) - amix_lower(*q);
		if (d != 0 || *p == '\0') return d;
		p++; q++;
	}
	return 0;
}

/* strsignal -- POSIX 2001, and absent from AMIX.  Wanted by any program with
 * a crash handler; OpenTTD's crashlog_unix.cpp uses it for one line of text.
 *
 * Our own table, not AMIX's _sys_siglist[]: that array is no export of
 * libc.so.1 but data in the link-time archive (siglist.o), so this shared
 * library would depend on the program having pulled it in, and a program
 * that loads libamixc99.so.1 only through another library has not
 * ("relocation error: symbol not found: _sys_siglist").  The numbers are
 * AMIX's <sys/signal.h> (SVR4, NSIG 32).
 */
static const char *const amix_signames[32] = {
	0,
	"Hangup",			/* SIGHUP     1 */
	"Interrupt",			/* SIGINT     2 */
	"Quit",				/* SIGQUIT    3 */
	"Illegal instruction",		/* SIGILL     4 */
	"Trace/breakpoint trap",	/* SIGTRAP    5 */
	"Abort",			/* SIGABRT    6 */
	"Emulation trap",		/* SIGEMT     7 */
	"Arithmetic exception",		/* SIGFPE     8 */
	"Killed",			/* SIGKILL    9 */
	"Bus error",			/* SIGBUS    10 */
	"Segmentation fault",		/* SIGSEGV   11 */
	"Bad system call",		/* SIGSYS    12 */
	"Broken pipe",			/* SIGPIPE   13 */
	"Alarm clock",			/* SIGALRM   14 */
	"Terminated",			/* SIGTERM   15 */
	"User signal 1",		/* SIGUSR1   16 */
	"User signal 2",		/* SIGUSR2   17 */
	"Child status changed",		/* SIGCHLD   18 */
	"Power fail/restart",		/* SIGPWR    19 */
	"Window size changed",		/* SIGWINCH  20 */
	"Urgent I/O condition",		/* SIGURG    21 */
	"Pollable event",		/* SIGPOLL   22 */
	"Stopped (signal)",		/* SIGSTOP   23 */
	"Stopped (user)",		/* SIGTSTP   24 */
	"Continued",			/* SIGCONT   25 */
	"Stopped (tty input)",		/* SIGTTIN   26 */
	"Stopped (tty output)",		/* SIGTTOU   27 */
	"Virtual timer expired",	/* SIGVTALRM 28 */
	"Profiling timer expired",	/* SIGPROF   29 */
	"CPU time limit exceeded",	/* SIGXCPU   30 */
	"File size limit exceeded"	/* SIGXFSZ   31 */
};

char *strsignal(int sig)
{
	if (sig > 0 && sig < (int)(sizeof amix_signames / sizeof amix_signames[0]))
		return (char *)amix_signames[sig];
	return (char *)"Unknown signal";
}
