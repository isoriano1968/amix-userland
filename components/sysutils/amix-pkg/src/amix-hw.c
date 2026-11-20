/*
 * amix-hw -- the boards this machine has (hw.c): one tag a line, for
 * scripts, the CDE session (Xsession.d/0013.amix-hw) and the installer.
 *
 *   amix-hw            z3660, z3660-core1, zz9000: those found
 *   amix-hw -q TAG     exit 0 if the machine has TAG (alternatives: a|b)
 *   amix-hw -v         and what the probe looked at, on stderr
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"

int
main(int argc, char **argv)
{
	const char *l;

	progname = "amix-hw";
	if (argc == 3 && !strcmp(argv[1], "-q"))
		return hw_has(argv[2]) ? 0 : 1;
	if (argc > 2 || (argc == 2 && strcmp(argv[1], "-v"))) {
		fprintf(stderr, "usage: amix-hw [-v] | -q tag[|tag...]\n");
		return 2;
	}
	l = hw_list();
	for (; *l; l++)
		putchar(*l == ' ' ? '\n' : *l);
	if (*hw_list())
		putchar('\n');
	if (argc == 2)
		fprintf(stderr, "amix-hw: %s\n", hw_why);
	return 0;
}
