/* vaarg.c -- is va_arg(ap, long long) broken when the va_list has been passed
 * to another function?
 *
 * The compat formatter dies on "%lld" and nothing else, and two suspects were
 * already cleared on this machine: 64-bit divide/modulo works, and
 * va_arg(ap, long long) works when read in the same function that called
 * va_start. The remaining structural difference is that snprintf hands its
 * va_list to vsnprintf and the 64-bit read happens there.
 *
 * If step 2 below dies, this is an ABI or toolchain fault, not a bug in the
 * formatter -- and it would affect every vsnprintf-style function a port
 * writes. */
#include <stdio.h>
#include <stdarg.h>

static void inner_int(va_list ap)
{
	int v = va_arg(ap, int);
	printf("  inner int:       %d (want 7)\n", v); fflush(stdout);
}

static void inner_ll(va_list ap)
{
	long long v = va_arg(ap, long long);
	printf("  inner long long: %ld (want 890123)\n", (long)(v % 1000000LL)); fflush(stdout);
}

static void outer_int(const char *tag, ...)
{
	va_list ap;
	va_start(ap, tag);
	inner_int(ap);
	va_end(ap);
}

static void outer_ll(const char *tag, ...)
{
	va_list ap;
	va_start(ap, tag);
	inner_ll(ap);
	va_end(ap);
}

static void outer_ll_copy(const char *tag, ...)
{
	va_list ap, ap2;
	va_start(ap, tag);
	__builtin_va_copy(ap2, ap);
	inner_ll(ap2);
	va_end(ap2);
	va_end(ap);
}

int main(void)
{
	printf("1 int through a passed va_list:\n"); fflush(stdout);
	outer_int("t", 7);

	printf("2 long long through a passed va_list:\n"); fflush(stdout);
	outer_ll("t", 1234567890123LL);

	printf("3 long long through a copied va_list:\n"); fflush(stdout);
	outer_ll_copy("t", 1234567890123LL);

	printf("done\n");
	return 0;
}
