/* ll.c -- the single call that kills the process, with nothing else in it. */
#include <stdio.h>
#include "../compat/amix_compat.h"

int main(void)
{
	char b[64];
	int n;

	printf("before\n"); fflush(stdout);
	n = amix_snprintf(b, sizeof b, "%lld", 1234567890123LL);
	printf("after n=%d b=\"%s\"\n", n, b); fflush(stdout);

	printf("plain %%d still works: ");  fflush(stdout);
	amix_snprintf(b, sizeof b, "%d", 7);
	printf("%s\n", b); fflush(stdout);
	return 0;
}
