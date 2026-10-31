/*
 * amix-math.c -- rounding and square roots with the 68060's own FPU
 * instructions.  AMIX's libm has no rint/lrint/round/trunc at all, and its
 * floor (2.5 us), ceil and sqrt (5.8 us, in software) do not use the FPU.
 * FFmpeg's fallbacks build lrintf on rint on floor/ceil, and lrintf runs
 * for every sound sample it converts to 16 bits.  amix_ names, because
 * libm's own objects are linked too; compat/include/amix-ff.h maps
 * FFmpeg's calls to them.
 *
 * fmove.l to a data register and fint round in the FPCR's mode, which is
 * round-to-nearest-even; fintrz truncates.  All are 68060 hardware
 * instructions (fdsqrt/fssqrt too), none is emulated.
 */
#include <math.h>

#ifdef __mc68000__

double amix_rint(double x)
{
	double r;

	__asm__("fint.x %1,%0" : "=f"(r) : "f"(x));
	return r;
}

float amix_rintf(float x)
{
	return (float)amix_rint(x);
}

double amix_trunc(double x)
{
	double r;

	__asm__("fintrz.x %1,%0" : "=f"(r) : "f"(x));
	return r;
}

float amix_truncf(float x)
{
	return (float)amix_trunc(x);
}

double amix_floor(double x)
{
	double t = amix_trunc(x);

	return t > x ? t - 1.0 : t;
}

float amix_floorf(float x)
{
	return (float)amix_floor(x);
}

double amix_ceil(double x)
{
	double t = amix_trunc(x);

	return t < x ? t + 1.0 : t;
}

float amix_ceilf(float x)
{
	return (float)amix_ceil(x);
}

/* halfway cases away from zero, as C99 says */
double amix_round(double x)
{
	return x >= 0 ? amix_trunc(x + 0.5) : -amix_trunc(0.5 - x);
}

float amix_roundf(float x)
{
	return (float)amix_round(x);
}

long amix_lrint(double x)
{
	long r;

	__asm__("fmove.l %1,%0" : "=d"(r) : "f"(x));
	return r;
}

long amix_lrintf(float x)
{
	long r;

	__asm__("fmove.l %1,%0" : "=d"(r) : "f"(x));
	return r;
}

long long amix_llrint(double x)
{
	return (long long)amix_rint(x);
}

long long amix_llrintf(float x)
{
	return (long long)amix_rint(x);
}

double amix_sqrt(double x)
{
	double r;

	__asm__("fdsqrt.x %1,%0" : "=f"(r) : "f"(x));	/* rounded to double */
	return r;
}

float amix_sqrtf(float x)
{
	float r;

	__asm__("fssqrt.x %1,%0" : "=f"(r) : "f"(x));	/* rounded to single */
	return r;
}

#endif

/* scalbn, which AMIX's libm lacks: x * 2^n through ldexp (radix 2). */
double scalbn(double x, int n)
{
	return ldexp(x, n);
}
