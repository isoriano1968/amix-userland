/* div64.c -- are libgcc's 64-bit divide and modulo routines sound?
 *
 * The earlier 64-bit probe divided by the CONSTANT 10, which GCC turns into a
 * multiply-by-reciprocal and never calls libgcc for. The compat formatter
 * divides by a variable base, so it makes real calls to __udivdi3 / __umoddi3,
 * and it dies. This tells the two apart.
 *
 * Everything is volatile so no divisor can be folded, and nothing here uses
 * 64-bit arithmetic to PRINT its results -- the halves go out through a union,
 * so a broken routine cannot take the printing with it. This is the same
 * discipline the AmigaOS port needed when libgcc's float conversions turned
 * out to return in the wrong register. */
#include <stdio.h>
#include <stdarg.h>

union split { unsigned long long v; unsigned long w[2]; };

static void show(const char *tag, unsigned long long v, unsigned long whi, unsigned long wlo)
{
	union split u;
	u.v = v;
	printf("%-26s hi=%08lx lo=%08lx  (want hi=%08lx lo=%08lx)\n",
	       tag, u.w[0], u.w[1], whi, wlo);
	fflush(stdout);
}

/* va_arg with no arithmetic anywhere near it. */
static void inner_raw(va_list ap)
{
	union split u;
	u.v = (unsigned long long)va_arg(ap, long long);
	printf("%-26s hi=%08lx lo=%08lx  (want hi=0000011f lo=71fb04cb)\n",
	       "va_arg in callee:", u.w[0], u.w[1]);
	fflush(stdout);
}

static void outer_raw(const char *tag, ...)
{
	va_list ap;
	va_start(ap, tag);
	inner_raw(ap);
	va_end(ap);
}

int main(void)
{
	volatile unsigned long long a = 1234567890123ULL;   /* 0x0000011F71FB04CB */
	volatile unsigned long long b = 10ULL;
	volatile long long sa = -1234567890123LL;
	volatile long long sb = 10LL;

	printf("step 1: va_arg, no arithmetic\n"); fflush(stdout);
	outer_raw("t", 1234567890123LL);

	printf("step 2: __udivdi3\n"); fflush(stdout);
	show("unsigned a / b:", a / b, 0x0000001cUL, 0xbe991a14UL);

	printf("step 3: __umoddi3\n"); fflush(stdout);
	show("unsigned a %% b:", a % b, 0x00000000UL, 0x00000003UL);

	printf("step 4: __divdi3\n"); fflush(stdout);
	show("signed sa / sb:", (unsigned long long)(sa / sb), 0xffffffe3UL, 0x4166e5ecUL);

	printf("step 5: __moddi3\n"); fflush(stdout);
	show("signed sa %% sb:", (unsigned long long)(sa % sb), 0xffffffffUL, 0xfffffffdUL);

	printf("done\n");
	return 0;
}
