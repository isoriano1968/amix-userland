/*
 * <math.h> for C99 on AMIX: AMIX's own <math.h> (SVR4.0, 1991), and on top
 * of it what C99 added, with the functions in libm99.so.1.
 *
 * Installed as /usr/local/include/amix-c99/math.h; `pkg-config --cflags
 * libm99` puts that directory first, so <math.h> means this file, and it
 * reaches AMIX's header (through GCC's include-fixed copy with gcc 14) with
 * #include_next.  Nothing of AMIX's is changed or hidden: its functions keep
 * their declarations and its libm.a (sin, exp, pow ...) and lib060 stay
 * what a program calls.  This adds:
 *
 *   - the declarations AMIX's header hides from a standard C compiler
 *     (__STDC__ == 1: erf, hypot, lgamma, cbrt, rint, M_PI ...), all of them
 *     in AMIX's libm.a;
 *   - float_t, double_t, NAN, INFINITY, HUGE_VALF, HUGE_VALL, FP_*,
 *     FP_ILOGB0/NAN, math_errhandling, and the classification and
 *     comparison macros (fpclassify, isnan, isinf, isfinite, isnormal,
 *     signbit, isgreater ...);
 *   - the C99 functions AMIX lacks: the float and long double variants and
 *     the new double ones, from libm99.so.1 (musl's libm).
 *
 * Compilers: written for gcc 14 (m68k-elf-gcc).  With AMIX's own gcc 2.7
 * the declarations and the macros work (the macros through libm99's
 * __fpclassify/__signbit functions, the comparisons evaluating their
 * arguments twice); <complex.h> needs gcc 14.
 *
 * Error handling: libm99's functions report through the FPU exception
 * flags (<fenv.h>), as musl does, and never set errno; AMIX's own set errno
 * and call matherr().  math_errhandling says MATH_ERREXCEPT.
 */
#ifndef _AMIX_C99_MATH_H
#define _AMIX_C99_MATH_H

#include_next <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && __GNUC__ >= 3
#define __M99_GNUC3 1
#else
#define __M99_GNUC3 0
#endif

/* ---- types -------------------------------------------------------------- */

/* as the compiler evaluates: 0 with gcc 14 -m68040/-m68060, 2 on the
 * 68881/68882 (gcc 14 -m68020/-m68030, gcc 2.7) */
#if defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ == 0
typedef float float_t;
typedef double double_t;
#elif defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ == 1
typedef double float_t;
typedef double double_t;
#else
typedef long double float_t;
typedef long double double_t;
#endif

/* ---- constants ---------------------------------------------------------- */

#if __M99_GNUC3
/* AMIX's HUGE_VAL is a variable (__huge_val.d): C99 wants a constant */
#undef HUGE_VAL
#define HUGE_VAL  (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY  (__builtin_inff())
#define NAN       (__builtin_nanf(""))
#else
#define HUGE_VALF ((float)HUGE_VAL)
#define HUGE_VALL ((long double)HUGE_VAL)
#define INFINITY  HUGE_VALF
#define NAN       (0.0f / 0.0f)
#endif

/* glibc's numbers, as GCC's include-fixed math.h already gives them */
#ifndef FP_NAN
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#endif

#define FP_ILOGBNAN (-1 - 0x7fffffff)
#define FP_ILOGB0   FP_ILOGBNAN

#define MATH_ERRNO       1
#define MATH_ERREXCEPT   2
#define math_errhandling MATH_ERREXCEPT

/* XSI: AMIX defines these only for a non-standard compiler */
#ifndef M_PI
#define M_E        2.7182818284590452354
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440
#endif
#ifndef MAXFLOAT
#define MAXFLOAT   ((float)3.40282346638528860e+38)
#endif

/* ---- classification ----------------------------------------------------- */

int __fpclassify(double);
int __fpclassifyf(float);
int __fpclassifyl(long double);
int __signbit(double);
int __signbitf(float);
int __signbitl(long double);

/* AMIX's libc has an isnan() function, which C99 makes a macro */
#undef isnan
#undef isinf
#undef isfinite
#undef isnormal
#undef signbit
#undef fpclassify

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))

#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf_sign(x)
#define isfinite(x)   __builtin_isfinite(x)
#define isnormal(x)   __builtin_isnormal(x)
#define signbit(x)    __builtin_signbit(x)
#define isgreater(x, y)      __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y)         __builtin_isless(x, y)
#define islessequal(x, y)    __builtin_islessequal(x, y)
#define islessgreater(x, y)  __builtin_islessgreater(x, y)
#define isunordered(x, y)    __builtin_isunordered(x, y)

#else	/* AMIX's gcc 2.7: through libm99's functions */

#define fpclassify(x) (sizeof(x) == sizeof(float) ? __fpclassifyf(x) : \
	sizeof(x) == sizeof(double) ? __fpclassify(x) : __fpclassifyl(x))
#define isnan(x)      (fpclassify(x) == FP_NAN)
#define isinf(x)      (fpclassify(x) == FP_INFINITE)
#define isfinite(x)   (fpclassify(x) > FP_INFINITE)
#define isnormal(x)   (fpclassify(x) == FP_NORMAL)
#define signbit(x)    (sizeof(x) == sizeof(float) ? __signbitf(x) : \
	sizeof(x) == sizeof(double) ? __signbit(x) : __signbitl(x))
#define isunordered(x, y)    (isnan(x) || isnan(y))
#define isgreater(x, y)      (!isunordered(x, y) && (x) > (y))
#define isgreaterequal(x, y) (!isunordered(x, y) && (x) >= (y))
#define isless(x, y)         (!isunordered(x, y) && (x) < (y))
#define islessequal(x, y)    (!isunordered(x, y) && (x) <= (y))
#define islessgreater(x, y)  (!isunordered(x, y) && ((x) < (y) || (x) > (y)))

#endif

/* ---- in AMIX's libm.a, declared by its header only without __STDC__ ----- */

extern double erf(double);
extern double erfc(double);
extern double hypot(double, double);
extern double lgamma(double);
extern double acosh(double);
extern double asinh(double);
extern double atanh(double);
extern double cbrt(double);
extern double copysign(double, double);
extern double rint(double);
extern double remainder(double, double);
extern double gamma(double);
extern double j0(double);
extern double j1(double);
extern double jn(int, double);
extern double y0(double);
extern double y1(double);
extern double yn(int, double);
extern int signgam;

/* ---- libm99: double ----------------------------------------------------- */

double exp2(double);
double expm1(double);
int    ilogb(double);
double log1p(double);
double log2(double);
double logb(double);
double scalbn(double, int);
double scalbln(double, long);
double tgamma(double);
double nearbyint(double);
long   lrint(double);
long long llrint(double);
double round(double);
long   lround(double);
long long llround(double);
double trunc(double);
double remquo(double, double, int *);
double nan(const char *);
double nextafter(double, double);
double nexttoward(double, long double);
double fdim(double, double);
double fmax(double, double);
double fmin(double, double);
double fma(double, double, double);
double lgamma_r(double, int *);
double scalb(double, double);
int    finite(double);

/* ---- libm99: float (AMIX has acosf ... fmodf, modff) -------------------- */

float acoshf(float);
float asinhf(float);
float atanhf(float);
float exp2f(float);
float expm1f(float);
float frexpf(float, int *);
int   ilogbf(float);
float ldexpf(float, int);
float log1pf(float);
float log2f(float);
float logbf(float);
float scalbnf(float, int);
float scalblnf(float, long);
float cbrtf(float);
float hypotf(float, float);
float erff(float);
float erfcf(float);
float lgammaf(float);
float tgammaf(float);
float nearbyintf(float);
float rintf(float);
long  lrintf(float);
long long llrintf(float);
float roundf(float);
long  lroundf(float);
long long llroundf(float);
float truncf(float);
float remainderf(float, float);
float remquof(float, float, int *);
float copysignf(float, float);
float nanf(const char *);
float nextafterf(float, float);
float nexttowardf(float, long double);
float fdimf(float, float);
float fmaxf(float, float);
float fminf(float, float);
float fmaf(float, float, float);
float lgammaf_r(float, int *);
float scalbf(float, float);
int   finitef(float);

/* ---- libm99: long double (68881 extended, 64-bit mantissa) -------------- */

long double acosl(long double);
long double asinl(long double);
long double atanl(long double);
long double atan2l(long double, long double);
long double cosl(long double);
long double sinl(long double);
long double tanl(long double);
long double acoshl(long double);
long double asinhl(long double);
long double atanhl(long double);
long double coshl(long double);
long double sinhl(long double);
long double tanhl(long double);
long double expl(long double);
long double exp2l(long double);
long double expm1l(long double);
long double frexpl(long double, int *);
int         ilogbl(long double);
long double ldexpl(long double, int);
long double logl(long double);
long double log10l(long double);
long double log1pl(long double);
long double log2l(long double);
long double logbl(long double);
long double modfl(long double, long double *);
long double scalbnl(long double, int);
long double scalblnl(long double, long);
long double cbrtl(long double);
long double fabsl(long double);
long double hypotl(long double, long double);
long double powl(long double, long double);
long double sqrtl(long double);
long double erfl(long double);
long double erfcl(long double);
long double lgammal(long double);
long double tgammal(long double);
long double ceill(long double);
long double floorl(long double);
long double nearbyintl(long double);
long double rintl(long double);
long        lrintl(long double);
long long   llrintl(long double);
long double roundl(long double);
long        lroundl(long double);
long long   llroundl(long double);
long double truncl(long double);
long double fmodl(long double, long double);
long double remainderl(long double, long double);
long double remquol(long double, long double, int *);
long double copysignl(long double, long double);
long double nanl(const char *);
long double nextafterl(long double, long double);
long double nexttowardl(long double, long double);
long double fdiml(long double, long double);
long double fmaxl(long double, long double);
long double fminl(long double, long double);
long double fmal(long double, long double, long double);
long double lgammal_r(long double, int *);

#ifdef __cplusplus
}
#endif

#endif /* _AMIX_C99_MATH_H */
