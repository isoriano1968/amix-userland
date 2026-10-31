/* CPython compatibility fallbacks, under the Python Software Foundation
 * License Version 2; see LICENSE.PSF in this directory.
 * expm1/log1p derive from CPython v3.10.16 Modules/_math.c; log2's finite
 * reduction derives from CPython v3.11.18 Modules/mathmodule.c.
 * These are private to the AMIX Python build, not a replacement system libm.
 */
#include <float.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
extern double copysign(double, double);

double nextafter(double x, double y)
{
    uint64_t bits;
    double result;
    _Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
                   "nextafter requires IEEE binary64");
    if (__builtin_isnan(x) || __builtin_isnan(y)) return x + y;
    if (x == y) return y;
    if (x == 0.0) bits = ((__builtin_signbit(y) ? 1ULL : 0ULL) << 63) | 1ULL;
    else {
        memcpy(&bits, &x, sizeof(bits));
        if ((x > 0.0) == (y > x)) bits++;
        else bits--;
    }
    memcpy(&result, &bits, sizeof(result));
    if (__builtin_isinf(result) || fabs(result) < DBL_MIN) errno = ERANGE;
    return result;
}

double exp2(double x)
{
    int exponent;
    if (__builtin_isnan(x)) return x;
    if (__builtin_isinf(x)) return x > 0.0 ? x : 0.0;
    if (x >= 1024.0) { errno = ERANGE; return __builtin_inf(); }
    if (x < -1075.0) { errno = ERANGE; return 0.0; }
    /* Reduce the exponential argument before scaling. Integer powers of two
     * are exact; this fallback has not passed a full libm accuracy suite. */
    exponent = (int)floor(x + 0.5);
    return ldexp(exp((x - exponent) * 0.69314718055994530942), exponent);
}

double round(double x)
{
    double magnitude = fabs(x), integral;
    if (!(magnitude < 4503599627370496.0)) return x;
    integral = floor(magnitude);
    if (magnitude - integral >= 0.5) integral += 1.0;
    return copysign(integral, x);
}

double expm1(double x)
{
    if (fabs(x) < 0.7) {
        double u = exp(x);
        if (u == 1.0)
            return x;
        return (u - 1.0) * x / log(u);
    }
    return exp(x) - 1.0;
}

double log1p(double x)
{
    if (fabs(x) < DBL_EPSILON / 2.0)
        return x;
    if (-0.5 <= x && x <= 1.0) {
        double y = 1.0 + x;
        return log(y) - ((y - 1.0) - x) / y;
    }
    return log(1.0 + x);
}

double log2(double x)
{
    double m;
    int e;
    if (__builtin_isnan(x) || x == __builtin_inf())
        return x;
    if (x == 0.0) {
        errno = ERANGE;
        return -__builtin_inf();
    }
    if (x < 0.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    m = frexp(x, &e);
    if (x >= 1.0)
        return log(2.0 * m) / log(2.0) + (e - 1);
    return log(m) / log(2.0) + e;
}
