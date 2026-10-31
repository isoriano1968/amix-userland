/* stderrprobe.c -- is stderr usable in a binary from this toolchain?
 *
 * OpenTTD on AMIX prints its help (printf, stdout) and then every startup
 * failure is silent: exit status 1 with zero bytes on both streams, including
 * with -d 3, whose debug output goes through fprintf(stderr) in 1.0.5.  A
 * program that can print to stdout and not to stderr would explain the whole
 * symptom, and it is not something to conclude by reading source.
 *
 * Each line says which route wrote it, so a partial failure is still legible.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	printf("1 stdout via printf\n");
	fprintf(stdout, "2 stdout via fprintf\n");
	fprintf(stderr, "3 stderr via fprintf\n");
	fputs("4 stderr via fputs\n", stderr);
	write(2, "5 stderr via write(2)\n", 22);
	write(1, "6 stdout via write(1)\n", 22);

	/* Unbuffered, in case the buffer is the problem rather than the stream. */
	setbuf(stderr, NULL);
	fprintf(stderr, "7 stderr via fprintf, unbuffered\n");

	fflush(NULL);
	printf("8 fflush(NULL) returned\n");

	/* Are the FILE pointers even distinct and non-null? */
	printf("9 stdout=%p stderr=%p fileno(stderr)=%d\n",
			(void *)stdout, (void *)stderr, fileno(stderr));
	fflush(stdout);
	return 0;
}
