/* amix_cpu.c -- which 680x0 this program runs on, for the few code paths
 * that may use 68040/68060-only instructions (move16, fdsqrt ...).
 *
 * Every AMIX package is built for the 68020 ISA tuned for the 68060
 * (doc/CPU-TARGET-68020-60.md in amix-userland): it runs on a 68030 A3000 and
 * on the Z3660's 68060 alike.  A library that has a faster path for the
 * 040/060 chooses it at run time with this, once (a so-init constructor or the
 * first call), and spells the instruction so that the assembler for the
 * baseline takes it (.word, or a local ".cpu 68060").
 *
 * The CPU comes from the kernel through uname(2), as Xzz9000's rtgcpu.c does
 * it: the 040/060 port kernel tags utsname.machine "Amiga (Unlimited)
 * 68040-<build>", the '4' turned into '6' when it runs on a 68060
 * (amix-gate port/src/inituname040.s).  A word "680x0-" names the CPU; the
 * stock 68030 kernel has no tag, and no tag means the baseline, 30.
 *
 * AMIX_CPU=20|30|40|60 in the environment overrides it, for tests (anything
 * else in it, e.g. a build's "-mcpu=..." flags, is ignored).
 *
 * C89, so that AMIX's own gcc 2.7 can use libamixc99 too (library/amixc99).
 */
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "amix_compat.h"
#include "include/amix_cpu.h"

static int amix_cpu_cached;

/* "680x0-" as a word of the machine string: 20, 30, 40, 60; else 0 */
int amix_cpu_from_machine(const char *machine)
{
	const char *p;

	for (p = machine; (p = strstr(p, "680")) != (const char *)0; p++)
		if ((p == machine || p[-1] == ' ') && p[4] == '0' && p[5] == '-' &&
		    (p[3] == '2' || p[3] == '3' || p[3] == '4' || p[3] == '6'))
			return (p[3] - '0') * 10;
	return 0;
}

static int amix_cpu_env(void)
{
	const char *e = getenv("AMIX_CPU");

	if (e == (const char *)0)
		return 0;
	if (!strcmp(e, "20") || !strcmp(e, "30") || !strcmp(e, "40") || !strcmp(e, "60"))
		return atoi(e);
	return 0;
}

int amix_cpu(void)
{
	struct utsname u;
	int cpu;

	if (amix_cpu_cached)
		return amix_cpu_cached;
	cpu = amix_cpu_env();
	if (!cpu && uname(&u) >= 0)
		cpu = amix_cpu_from_machine(u.machine);
	if (!cpu)
		cpu = AMIX_CPU_BASELINE;
	amix_cpu_cached = cpu;
	return cpu;
}
