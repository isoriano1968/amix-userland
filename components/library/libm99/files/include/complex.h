/*
 * <complex.h> for C99 on AMIX, with the functions in libm99.so.1 (musl's).
 * AMIX has no complex arithmetic; this needs gcc 14 (_Complex), the native
 * gcc 2.7 has none.  Installed as /usr/local/include/amix-c99/complex.h.
 *
 * Accuracy is musl's (measured against glibc, tests/cases.h): the double and
 * float functions within a few ulps, except casin, cacos, casinh and cacosh
 * (textbook formulas, up to ~100 ulps).  Of the long double ones, cabsl,
 * cargl, catanl, catanhl, conjl, cprojl and the parts are full 64-bit;
 * csqrtl, cexpl, clogl, ccoshl, csinhl, ctanhl compute in double, and
 * ccosl, csinl, ctanl, cpowl go through them (about double precision);
 * casinl, cacosl, casinhl, cacoshl use the textbook formulas in long double.
 * C99 Annex G (infinite and NaN parts) is not followed everywhere: catan,
 * catanh, casin and relatives give NaN where glibc gives a value.
 */
#ifndef _AMIX_C99_COMPLEX_H
#define _AMIX_C99_COMPLEX_H

#if !defined(__GNUC__) || __GNUC__ < 3
#error "<complex.h> needs gcc 14: AMIX's gcc 2.7 has no _Complex"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define complex _Complex
#define _Complex_I (__extension__ 1.0iF)
#undef I
#define I _Complex_I

#define CMPLX(x, y)  __builtin_complex((double)(x), (double)(y))
#define CMPLXF(x, y) __builtin_complex((float)(x), (float)(y))
#define CMPLXL(x, y) __builtin_complex((long double)(x), (long double)(y))

double complex cacos(double complex);
float complex cacosf(float complex);
long double complex cacosl(long double complex);
double complex casin(double complex);
float complex casinf(float complex);
long double complex casinl(long double complex);
double complex catan(double complex);
float complex catanf(float complex);
long double complex catanl(long double complex);
double complex ccos(double complex);
float complex ccosf(float complex);
long double complex ccosl(long double complex);
double complex csin(double complex);
float complex csinf(float complex);
long double complex csinl(long double complex);
double complex ctan(double complex);
float complex ctanf(float complex);
long double complex ctanl(long double complex);
double complex cacosh(double complex);
float complex cacoshf(float complex);
long double complex cacoshl(long double complex);
double complex casinh(double complex);
float complex casinhf(float complex);
long double complex casinhl(long double complex);
double complex catanh(double complex);
float complex catanhf(float complex);
long double complex catanhl(long double complex);
double complex ccosh(double complex);
float complex ccoshf(float complex);
long double complex ccoshl(long double complex);
double complex csinh(double complex);
float complex csinhf(float complex);
long double complex csinhl(long double complex);
double complex ctanh(double complex);
float complex ctanhf(float complex);
long double complex ctanhl(long double complex);
double complex cexp(double complex);
float complex cexpf(float complex);
long double complex cexpl(long double complex);
double complex clog(double complex);
float complex clogf(float complex);
long double complex clogl(long double complex);
double cabs(double complex);
float cabsf(float complex);
long double cabsl(long double complex);
double complex cpow(double complex, double complex);
float complex cpowf(float complex, float complex);
long double complex cpowl(long double complex, long double complex);
double complex csqrt(double complex);
float complex csqrtf(float complex);
long double complex csqrtl(long double complex);
double carg(double complex);
float cargf(float complex);
long double cargl(long double complex);
double cimag(double complex);
float cimagf(float complex);
long double cimagl(long double complex);
double complex conj(double complex);
float complex conjf(float complex);
long double complex conjl(long double complex);
double complex cproj(double complex);
float complex cprojf(float complex);
long double complex cprojl(long double complex);
double creal(double complex);
float crealf(float complex);
long double creall(long double complex);

#ifdef __cplusplus
}
#endif

#endif /* _AMIX_C99_COMPLEX_H */
