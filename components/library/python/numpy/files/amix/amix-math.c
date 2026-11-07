/* amix-math.c -- linked into every numpy module (files/amix-cc.in, and the
 * libm_link_args of the cross file), hidden: two maths functions a module
 * cannot take from where AMIX has them.  AMIX's libc has modff only in its
 * static part (libc.a, not libc.so.1), and libm99 leaves it out; libm.a's
 * rint calls _fpgetround, also only in libc.a.  rint is FINT in the current
 * rounding mode (the 060 executes it in hardware, unlike the 040); modff is
 * exact through modf (libc.so.1).  gcc 14 only. */
#include <math.h>

#define HIDDEN __attribute__((visibility("hidden")))

HIDDEN double rint(double x)
{
	double r;

	__asm__("fint%.x %1,%0" : "=f" (r) : "f" (x));
	return r;
}

HIDDEN float modff(float x, float *ip)
{
	double i, f = modf((double)x, &i);

	*ip = (float)i;
	return (float)f;
}
