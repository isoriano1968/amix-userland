/* amix-ieee-math.h -- forced into QuickJS's sources (-include): AMIX's own
 * C89 maths (libc.a and libm.a, SVR4.0, 1991) is not IEEE 754.  floor() and
 * ceil() go through modf(), which sends the process SIGFPE for a NaN;
 * fmod(x, 0) returns x; fmod, pow, log, sqrt ... print "DOMAIN error"
 * (matherr) and return 0 or HUGE.  JavaScript passes NaN and Infinity to
 * them all the time, so each name below goes to a wrapper in
 * amix-ieee-math.c around musl's long double function from library/libm99.
 * Object-like macros, so that QuickJS's tables of Math function pointers
 * are renamed too, and gcc no longer sees a builtin it could fold back
 * into AMIX's function.  frexp, ldexp and fabs stay AMIX's (exact for
 * finite numbers; QuickJS checks for NaN and Infinity before them). */
#ifndef AMIX_IEEE_MATH_H
#define AMIX_IEEE_MATH_H
#define acos   amix_ieee_acos
#define asin   amix_ieee_asin
#define atan   amix_ieee_atan
#define ceil   amix_ieee_ceil
#define cos    amix_ieee_cos
#define cosh   amix_ieee_cosh
#define exp    amix_ieee_exp
#define floor  amix_ieee_floor
#define log    amix_ieee_log
#define log10  amix_ieee_log10
#define sin    amix_ieee_sin
#define sinh   amix_ieee_sinh
#define sqrt   amix_ieee_sqrt
#define tan    amix_ieee_tan
#define tanh   amix_ieee_tanh
#define asinh  amix_ieee_asinh
#define acosh  amix_ieee_acosh
#define atanh  amix_ieee_atanh
#define atan2  amix_ieee_atan2
#define fmod   amix_ieee_fmod
#define pow    amix_ieee_pow
#endif
