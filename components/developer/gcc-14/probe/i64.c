/* i64.c -- two separate questions about 64-bit integers, because the compat
 * formatter's %lld path kills the process and either one could be the cause.
 *
 *   1. Does libgcc's 64-bit divide/modulo work at all on this target?
 *      A broken one makes a digit loop never terminate, which then walks off
 *      the end of its stack buffer -- consistent with the SIGKILL we saw.
 *   2. Does va_arg(ap, long long) read the right eight bytes?
 *
 * Every loop here is bounded, so a wrong answer prints a wrong number instead
 * of taking the process with it. */
#include <stdio.h>
#include <stdarg.h>

static void digits_of(unsigned long long uv, char *out)
{
	char tmp[32];
	int n = 0;
	/* Bounded on purpose: 20 digits is the most a 64-bit value can have, so
	 * more than that means the division is not converging. */
	do {
		tmp[n++] = (char)('0' + (int)(uv % 10ULL));
		uv /= 10ULL;
	} while (uv != 0ULL && n < 21);
	if (n >= 21) { out[0] = '!'; out[1] = '\0'; return; }
	{ int i; for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i]; out[n] = '\0'; }
}

static void varargs_probe(const char *tag, ...)
{
	va_list ap;
	unsigned long long uv;
	char buf[32];

	va_start(ap, tag);
	uv = va_arg(ap, unsigned long long);
	va_end(ap);
	digits_of(uv, buf);
	printf("%-24s %s (want 1234567890123)\n", tag, buf);
	fflush(stdout);
}

int main(void)
{
	unsigned long long uv = 1234567890123ULL;
	char buf[32];

	printf("div64:  %lu (want 123456789012)\n", (unsigned long)(uv / 10ULL));
	fflush(stdout);
	printf("mod64:  %lu (want 3)\n", (unsigned long)(uv % 10ULL));
	fflush(stdout);

	digits_of(uv, buf);
	printf("%-24s %s (want 1234567890123)\n", "digit loop:", buf);
	fflush(stdout);

	varargs_probe("va_arg long long:", uv);

	printf("done\n");
	return 0;
}
