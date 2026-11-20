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
 * instructions; fdsqrt/fssqrt (040/060 only) are chosen at run time, fsqrt
 * on a 68881/68882 (has_dsqrt below).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

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

/* the FPU rounds square roots straight to double or single itself on the
 * 68040/060 (fdsqrt/fssqrt); a 68881/68882 has only fsqrt, in extended, and
 * the result is rounded once more on the store.  Which one: uname, once (the
 * port kernel names the CPU, "68040-"/"68060-"; stock AMIX, a 68030, none);
 * AMIX_CPU=30|40|60 decides in tests.  The 040/060 forms are spelled as
 * words with fixed registers: the 68020-68060 baseline assembler refuses
 * them, and they run only where uname named a 68040/060. */
static int dsqrt_ok = -1;

static int has_dsqrt(void)
{
	if (dsqrt_ok < 0) {
		const char *e = getenv("AMIX_CPU");
		struct utsname u;

		if (e && *e)
			dsqrt_ok = strstr(e, "40") != 0 || strstr(e, "60") != 0;
		else
			dsqrt_ok = uname(&u) >= 0 && (strstr(u.machine, "68040") != 0 ||
						      strstr(u.machine, "68060") != 0);
	}
	return dsqrt_ok;
}

double amix_sqrt(double x)
{
	if (has_dsqrt()) {
		register double r __asm__("fp0") = x;

		__asm__(".word 0xf200,0x0045" : "+f"(r));	/* fdsqrt.x %fp0,%fp0: rounded to double */
		return r;
	} else {
		double r;

		__asm__("fsqrt.x %1,%0" : "=f"(r) : "f"(x));
		return r;
	}
}

float amix_sqrtf(float x)
{
	if (has_dsqrt()) {
		register float r __asm__("fp0") = x;

		__asm__(".word 0xf200,0x0041" : "+f"(r));	/* fssqrt.x %fp0,%fp0: rounded to single */
		return r;
	} else {
		float r;

		__asm__("fsqrt.x %1,%0" : "=f"(r) : "f"(x));
		return r;
	}
}

#endif

/* scalbn, which AMIX's libm lacks: x * 2^n through ldexp (radix 2). */
double scalbn(double x, int n)
{
	return ldexp(x, n);
}
