/* compat.c -- does the borrowed-formatter snprintf actually behave like C99?
 *
 * The interesting cases are the ones where a naive implementation is wrong:
 * exact fit, truncation, the return value when truncated (C99 wants the length
 * it WOULD have been), a zero-size buffer, and output past the stack fast path
 * so the malloc branch runs too. */
#include <stdio.h>
#include <string.h>
#include "../compat/amix_compat.h"

static int fails = 0;

static void expect_str(const char *what, const char *got, const char *want)
{
	int ok = strcmp(got, want) == 0;
	if (!ok) fails++;
	printf("%-22s %-5s got \"%s\" want \"%s\"\n", what, ok ? "ok" : "FAIL", got, want);
	fflush(stdout);
}

static void expect_int(const char *what, int got, int want)
{
	int ok = got == want;
	if (!ok) fails++;
	printf("%-22s %-5s got %d want %d\n", what, ok ? "ok" : "FAIL", got, want);
}

int main(void)
{
	char b[8];
	char big[1200];
	int n;

	n = amix_snprintf(b, sizeof b, "%d-%s", 42, "ok");
	expect_str("plain", b, "42-ok");
	expect_int("plain return", n, 5);

	/* Exactly fills the buffer including the terminator. */
	n = amix_snprintf(b, sizeof b, "%s", "1234567");
	expect_str("exact fit", b, "1234567");
	expect_int("exact fit return", n, 7);

	/* One too long: truncated, but the return value is the full length. */
	n = amix_snprintf(b, sizeof b, "%s", "12345678");
	expect_str("truncated", b, "1234567");
	expect_int("truncated return", n, 8);

	/* Length enquiry with no buffer at all. */
	expect_int("NULL/0 return", amix_snprintf(NULL, 0, "%s%d", "abc", 1000), 7);

	/* Past the stack fast path, so the malloc branch runs. */
	n = amix_snprintf(big, sizeof big, "%*d", 1000, 7);
	expect_int("wide 1000 return", n, 1000);
	expect_int("wide 1000 length", (int)strlen(big), 1000);

	/* The conversions OpenTTD leans on, one at a time: a battery that dies
	 * halfway tells you which conversion killed it. Flush after each. */
	amix_snprintf(big, sizeof big, "%s", "str");    expect_str("conv %s", big, "str");
	amix_snprintf(big, sizeof big, "%d", -7);       expect_str("conv %d", big, "-7");
	amix_snprintf(big, sizeof big, "%u", 7u);       expect_str("conv %u", big, "7");
	amix_snprintf(big, sizeof big, "%x", 255);      expect_str("conv %x", big, "ff");
	amix_snprintf(big, sizeof big, "%X", 255);      expect_str("conv %X", big, "FF");
	amix_snprintf(big, sizeof big, "%c", 'A');      expect_str("conv %c", big, "A");
	amix_snprintf(big, sizeof big, "%o", 8);        expect_str("conv %o", big, "10");
	amix_snprintf(big, sizeof big, "%08.3d", 42);   expect_str("conv %08.3d", big, "     042");
	amix_snprintf(big, sizeof big, "%-6d|", 42);    expect_str("conv %-6d", big, "42    |");
	amix_snprintf(big, sizeof big, "%5.2f", 1.5);   expect_str("conv %5.2f", big, " 1.50");
	amix_snprintf(big, sizeof big, "%lld", 1234567890123LL);
	expect_str("conv %lld", big, "1234567890123");
	amix_snprintf(big, sizeof big, "%lld", -1234567890123LL);
	expect_str("conv -%lld", big, "-1234567890123");
	amix_snprintf(big, sizeof big, "%llx", 0xABCDEF0123ULL);
	expect_str("conv %llx", big, "abcdef0123");
	amix_snprintf(big, sizeof big, "%016lld", 42LL);
	expect_str("conv %016lld", big, "0000000000000042");
	amix_snprintf(big, sizeof big, "a%sb%dc%llde", "S", 1, 2LL);
	expect_str("conv mixed", big, "aSb1c2e");

	printf(fails == 0 ? "all ok\n" : "FAILURES: %d\n", fails);
	return fails == 0 ? 0 : 1;
}
