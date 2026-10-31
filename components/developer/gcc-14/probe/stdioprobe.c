/* stdioprobe.c -- which stdio call hangs on AMIX with this toolchain?
 *
 * stderrprobe.c printed its first printf and then never returned, taking its
 * login shell with it, and it does the same with output redirected to a file --
 * so the hang is in the process, not in terminal handling.  With stdout going
 * to a file everything is fully buffered and invisible, which is exactly the
 * condition that hides where it stopped.
 *
 * So every step is bracketed by write(1, ...), which is a syscall and cannot
 * buffer, cannot be reordered by a FILE, and does not depend on the thing under
 * test working.  Whatever marker comes out last names the call that hung.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define MARK(s) write(1, "[" s "]\n", (unsigned)strlen(s) + 3)

int main(void)
{
	MARK("start");

	MARK("before printf");
	printf("printf output\n");
	MARK("after printf");

	MARK("before fflush(stdout)");
	fflush(stdout);
	MARK("after fflush(stdout)");

	MARK("before fprintf(stdout)");
	fprintf(stdout, "fprintf stdout output\n");
	MARK("after fprintf(stdout)");

	MARK("before fprintf(stderr)");
	fprintf(stderr, "fprintf stderr output\n");
	MARK("after fprintf(stderr)");

	MARK("before fputs(stderr)");
	fputs("fputs stderr output\n", stderr);
	MARK("after fputs(stderr)");

	MARK("before fflush(NULL)");
	fflush(NULL);
	MARK("after fflush(NULL)");

	MARK("done -- returning from main");
	return 0;
}
