/* interop.c -- prove that code from a modern m68k GCC and the 1991 AMIX libc
 * agree on the ABI.  Every line here failed for a reason once, on some port:
 * type sizes, struct alignment, 64-bit helper routines out of libgcc, the
 * varargs layout printf reads, and malloc/stdio ownership across the boundary.
 *
 * Floating point is asked LAST and separately: the target runs a 68060, whose
 * FPU lacks instructions the 68881 has, and the kernel's ifpsp060 emulation is
 * not yet complete.  If the run stops at the "fp:" line, that is the answer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* AMIX's <sys/time.h> carries no gettimeofday prototype even though libc
 * exports the symbol -- the SVR4.0 headers are incomplete against their own
 * library, so a port has to declare what it uses.  This is the kind of gap the
 * amix-compat layer exists to hold. */
extern int gettimeofday(struct timeval *, void *);

struct packing { char c; int i; short s; };

static long long mul64(long long a, long long b) { return a * b; }
static unsigned long long div64(unsigned long long a, unsigned long long b) { return a / b; }

int main(void)
{
	struct packing p;
	char *heap;
	struct timeval tv;
	long long big;
	double d;

	printf("sizes:   short=%d int=%d long=%d ptr=%d longlong=%d\n",
	       (int)sizeof(short), (int)sizeof(int), (int)sizeof(long),
	       (int)sizeof(void *), (int)sizeof(long long));
	printf("packing: sizeof=%d offsetof(i)=%d   (SVR4 m68k wants 8 and 2)\n",
	       (int)sizeof(struct packing), (int)((char *)&p.i - (char *)&p));
	printf("char:    %s\n", ((char)-1 < 0) ? "signed" : "unsigned");

	/* 64-bit arithmetic goes through libgcc helper routines; if the modern
	 * libgcc were ABI-incompatible with our own code this comes out wrong. */
	big = mul64(1000000007LL, 1234567LL);
	printf("mul64:   %ld (low 9 digits, want 8641969)\n",
	       (long)(big % 1000000000LL));
	printf("div64:   %lu (want 1234567890)\n",
	       (unsigned long)div64(12345678901234567890ULL, 10000000000ULL));

	/* libc ownership: malloc here, free here, string ops from libc. */
	heap = (char *)malloc(64);
	if (heap == NULL) { printf("malloc:  FAILED\n"); return 1; }
	strcpy(heap, "libc string functions");
	printf("heap:    %s (len %d)\n", heap, (int)strlen(heap));
	free(heap);

	if (gettimeofday(&tv, NULL) == 0) {
		printf("time:    tv_sec=%lu\n", (unsigned long)tv.tv_sec);
	} else {
		printf("time:    gettimeofday FAILED\n");
	}

	/* Last, and on purpose: floating point. */
	fflush(stdout);
	d = 1.0;
	d = d / 3.0;
	printf("fp:      1/3 = %.6f (want 0.333333)\n", d);
	printf("done\n");
	return 0;
}
