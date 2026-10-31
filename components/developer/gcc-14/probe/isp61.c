/* isp61.c -- which of the instructions the 68060 removed does this kernel
 * emulate?
 *
 * The 68060 dropped several integer instructions that the 68020 and 68040 have,
 * and they are meant to be emulated by Motorola's Integer Support Package
 * through vector 61.  Whether a given kernel has that, and how completely, is
 * not something a program can ask -- it can only try.
 *
 * ONE INSTRUCTION PER PROCESS, selected by argv[1].  A signal handler would be
 * the obvious design and is the wrong one: a kernel that declines to emulate
 * may deliver SIGKILL, which cannot be caught, so a harness that tried to
 * survive would simply die on the first failing case and report nothing about
 * the rest.  Letting the shell observe each exit status costs a fork and tells
 * the whole story.
 *
 * The two forms that matter are easy to confuse, and only one of each pair was
 * removed:
 *   divul.l  Dn,Dr:Dq   32-bit dividend, quotient and remainder   KEPT
 *   divu.l   Dn,Dr:Dq   64-bit dividend                           REMOVED
 *   mulu.l   Dn,Dl      32x32 -> 32                               KEPT
 *   mulu.l   Dn,Dh:Dl   32x32 -> 64                               REMOVED
 *
 * Build with -mcpu=68020: the assembler refuses the removed forms when told the
 * target is a 68060, which is the whole point of telling it that.
 *
 * When recording a result, name the IMAGE and the CPU separately. uname's
 * prefix is a readout of the CPU running the image, not the image's build id,
 * so "the 68060 kernel" is not a thing: one image boots both.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *what = (argc > 1) ? argv[1] : "";
	unsigned long lo = 0, hi = 0;

	if (strcmp(what, "mul32") == 0) {
		/* KEPT: 32x32 -> 32. */
		lo = 100000; hi = 0;
		__asm__ volatile ("mulu.l %1,%0" : "+d"(lo) : "d"(70000UL));
		printf("mul32   ok  %lu (want 2172000000)\n", lo);

	} else if (strcmp(what, "mul64") == 0) {
		/* REMOVED: 32x32 -> 64, product in Dh:Dl. 100000 * 70000 = 7e9,
		 * which is 0x1_A13B_8600 -- hi=1, lo=0xA13B8600. */
		lo = 100000;
		__asm__ volatile ("mulu.l %2,%1:%0" : "+d"(lo), "=d"(hi) : "d"(70000UL));
		printf("mul64   ok  hi=0x%08lx lo=0x%08lx (want hi=00000001 lo=a13b8600)\n", hi, lo);

	} else if (strcmp(what, "divul") == 0) {
		/* KEPT: 32-bit dividend, quotient and remainder. 1000000 / 7. */
		lo = 1000000; hi = 0;
		__asm__ volatile ("divul.l %2,%1:%0" : "+d"(lo), "+d"(hi) : "d"(7UL));
		printf("divul   ok  q=%lu r=%lu (want 142857 and 1)\n", lo, hi);

	} else if (strcmp(what, "div64") == 0) {
		/* REMOVED: 64-bit dividend in Dr:Dq. 0x1_00000000 / 7. */
		lo = 0; hi = 1;
		__asm__ volatile ("divu.l %2,%1:%0" : "+d"(lo), "+d"(hi) : "d"(7UL));
		printf("div64   ok  q=%lu r=%lu (want 613566756 and 4)\n", lo, hi);

	} else {
		printf("usage: isp61 mul32|mul64|divul|div64\n");
		return 2;
	}
	fflush(stdout);
	return 0;
}
