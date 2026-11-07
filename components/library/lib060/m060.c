/*
 * m060.c -- sin, cos, tan and fmod for AMIX on the 68060 (2026-10-26).
 *
 * The 060 has no FSIN/FCOS/FTAN/FMOD/FREM: each one traps into the
 * kernel's emulation, 78 us per fsin on the A3000/Z3660, and AMIX's own
 * libm sin still takes 31 us.  These use only what the 060 does in
 * hardware (add, mul, FINTRZ): Cody-Waite reduction by pi/2 and fdlibm's
 * kernel polynomials, ~1-2 us, full double precision for |x| < 2^19*pi/2;
 * beyond that (never in graphics) x is first taken modulo 2*pi, which
 * loses the low bits.  (AMIX libm's sin cannot be called for it: its
 * object defines sin and cos too.)
 *
 * The other libm060.a members (atan ... j1, AMIX's FMOVECR users) are
 * musl's: see the Makefile.
 *
 * Link before -lm, and compile callers with -fno-builtin-sin
 * -fno-builtin-cos -fno-builtin-tan -fno-builtin-fmod (and the f
 * variants): gcc -ffast-math otherwise inlines the trapping instructions.
 */
#include <math.h>

#define TWOPI   6.28318530717958647692

#define PIO2_1  1.57079632673412561417e+00      /* first 33 bits of pi/2 */
#define PIO2_1T 6.07710050650619224932e-11      /* pi/2 - PIO2_1 */
#define PIO2_2  6.07710050630396597660e-11      /* second 33 bits */
#define PIO2_2T 2.02226624879595063154e-21
#define INVPIO2 6.36619772367581382433e-01

static const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
    S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
    S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
static const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
    C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
    C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;

static double ksin(double x)                    /* |x| <= pi/4 */
{
    double z = x * x;
    return x + x * z * (S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)))));
}

static double kcos(double x)                    /* |x| <= pi/4 */
{
    double z = x * x;
    return 1.0 - 0.5 * z + z * z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
}

/* x = n*pi/2 + r, |r| <= pi/4 (a little more at rounding); 0 if too big */
static int reduce(double x, double *r, long *n)
{
    double fn, ax = x < 0 ? -x : x;

    if (ax > 823549.6)                          /* 2^19 * pi/2 */
        return 0;
    fn = x * INVPIO2;
    fn = fn < 0 ? fn - 0.5 : fn + 0.5;
    *n = (long) fn;                             /* FINTRZ, then fmove.l */
    fn = (double) *n;
    *r = ((x - fn * PIO2_1) - fn * PIO2_1T) ;
    return 1;
}

double sin(double x)
{
    double r;
    long n;

    if (x > -0.7853981633974483 && x < 0.7853981633974483)
        return ksin(x);
    if (!reduce(x, &r, &n))
        return sin(fmod(x, TWOPI));
    switch (n & 3) {
    case 0:  return ksin(r);
    case 1:  return kcos(r);
    case 2:  return -ksin(r);
    default: return -kcos(r);
    }
}

double cos(double x)
{
    double r;
    long n;

    if (x > -0.7853981633974483 && x < 0.7853981633974483)
        return kcos(x);
    if (!reduce(x, &r, &n))
        return cos(fmod(x, TWOPI));
    switch (n & 3) {
    case 0:  return kcos(r);
    case 1:  return -ksin(r);
    case 2:  return -kcos(r);
    default: return ksin(r);
    }
}

double tan(double x)
{
    double r;
    long n;

    if (!reduce(x, &r, &n))
        return tan(fmod(x, TWOPI));
    return (n & 1) ? -kcos(r) / ksin(r) : ksin(r) / kcos(r);
}

float sinf(float x) { return (float) sin(x); }
float cosf(float x) { return (float) cos(x); }
float tanf(float x) { return (float) tan(x); }

/*
 * fmod: x - trunc(x/y)*y.  Exact while the quotient fits 2^52 and its
 * product with y does (the subtraction of a multiple of y below |x| is
 * exact then); C's sign rules (result has x's sign).
 */
double fmod(double x, double y)
{
    double q, ay = y < 0 ? -y : y, ax = x < 0 ? -x : x, r;

    if (y == 0.0 || x != x || y != y || ax > 1.7976931348623157e308)
        return (x * y) / (x * y);               /* NaN, as the standard says */
    if (ax < ay)
        return x;
    q = ax / ay;
    if (q >= 4503599627370496.0) {              /* 2^52: reduce in steps */
        r = ax;
        while (r >= ay) {
            double d = ay;
            while (d * 2.0 <= r)
                d *= 2.0;
            r -= d;
        }
    } else {
        q = q < 2147483647.0 ? (double) (long) q       /* FINTRZ */
                             : (double) (long long) q;
        r = ax - q * ay;
        if (r < 0)
            r += ay;
        else if (r >= ay)
            r -= ay;
    }
    return x < 0 ? -r : r;
}

float fmodf(float x, float y) { return (float) fmod(x, y); }

/*
 * sqrt: the 060 has FSQRT; AMIX's libm computes it in software (5.8 us,
 * sqrtf 10 us).  FDSQRT rounds straight to double, so the result is the
 * correctly rounded one (FSQRT.X would round to extended first, then to
 * double when stored).  A negative argument gives a NaN (AMIX's prints a
 * DOMAIN message through matherr and returns 0).
 */
double sqrt(double x)
{
    double r;

    if (x < 0.0)
        return (x - x) / (x - x);
    __asm__ ("fdsqrt%.x %1,%0" : "=f" (r) : "f" (x));
    return r;
}

float sqrtf(float x) { return (float) sqrt(x); }

/* atan atan2 atanf atan2f hypot pow erf erfc lgamma j0 j1 y0 y1 jn yn: musl's,
   built from the release into separate members (see the Makefile) */
