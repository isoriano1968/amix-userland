/* amix-ieee-math.c -- the wrappers amix-ieee-math.h names: musl's long
 * double functions from library/libm99, rounded to double (exact for floor,
 * ceil and fmod; correctly rounded in practice for the rest).  Declared
 * under other C names with asm labels, or gcc folds (double) floorl(x) back
 * into floor(x).  Not compiled with amix-ieee-math.h. */
extern long double amix_m99_acosl(long double) __asm__("acosl");
extern long double amix_m99_asinl(long double) __asm__("asinl");
extern long double amix_m99_atanl(long double) __asm__("atanl");
extern long double amix_m99_ceill(long double) __asm__("ceill");
extern long double amix_m99_cosl(long double) __asm__("cosl");
extern long double amix_m99_coshl(long double) __asm__("coshl");
extern long double amix_m99_expl(long double) __asm__("expl");
extern long double amix_m99_floorl(long double) __asm__("floorl");
extern long double amix_m99_logl(long double) __asm__("logl");
extern long double amix_m99_log10l(long double) __asm__("log10l");
extern long double amix_m99_sinl(long double) __asm__("sinl");
extern long double amix_m99_sinhl(long double) __asm__("sinhl");
extern long double amix_m99_sqrtl(long double) __asm__("sqrtl");
extern long double amix_m99_tanl(long double) __asm__("tanl");
extern long double amix_m99_tanhl(long double) __asm__("tanhl");
extern long double amix_m99_asinhl(long double) __asm__("asinhl");
extern long double amix_m99_acoshl(long double) __asm__("acoshl");
extern long double amix_m99_atanhl(long double) __asm__("atanhl");
extern long double amix_m99_atan2l(long double, long double) __asm__("atan2l");
extern long double amix_m99_fmodl(long double, long double) __asm__("fmodl");
extern long double amix_m99_powl(long double, long double) __asm__("powl");

double amix_ieee_acos(double x) { return (double) amix_m99_acosl(x); }
double amix_ieee_asin(double x) { return (double) amix_m99_asinl(x); }
double amix_ieee_atan(double x) { return (double) amix_m99_atanl(x); }
double amix_ieee_ceil(double x) { return (double) amix_m99_ceill(x); }
double amix_ieee_cos(double x) { return (double) amix_m99_cosl(x); }
double amix_ieee_cosh(double x) { return (double) amix_m99_coshl(x); }
double amix_ieee_exp(double x) { return (double) amix_m99_expl(x); }
double amix_ieee_floor(double x) { return (double) amix_m99_floorl(x); }
double amix_ieee_log(double x) { return (double) amix_m99_logl(x); }
double amix_ieee_log10(double x) { return (double) amix_m99_log10l(x); }
double amix_ieee_sin(double x) { return (double) amix_m99_sinl(x); }
double amix_ieee_sinh(double x) { return (double) amix_m99_sinhl(x); }
double amix_ieee_sqrt(double x) { return (double) amix_m99_sqrtl(x); }
double amix_ieee_tan(double x) { return (double) amix_m99_tanl(x); }
double amix_ieee_tanh(double x) { return (double) amix_m99_tanhl(x); }
double amix_ieee_asinh(double x) { return (double) amix_m99_asinhl(x); }
double amix_ieee_acosh(double x) { return (double) amix_m99_acoshl(x); }
double amix_ieee_atanh(double x) { return (double) amix_m99_atanhl(x); }
double amix_ieee_atan2(double x, double y) { return (double) amix_m99_atan2l(x, y); }
double amix_ieee_fmod(double x, double y) { return (double) amix_m99_fmodl(x, y); }
double amix_ieee_pow(double x, double y) { return (double) amix_m99_powl(x, y); }
