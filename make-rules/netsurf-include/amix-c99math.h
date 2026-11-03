/*
 * C99 float functions AMIX's libm (C89, double only) lacks, on the double
 * ones, and isnan, which math.h declares only when __STDC__ is 0; a
 * NetSurf library that uses them takes this with -include.
 */
#ifndef AMIX_C99MATH_H
#define AMIX_C99MATH_H
#include <math.h>

static inline float amix_ceilf(float x) { return (float)ceil(x); }
static inline float amix_floorf(float x) { return (float)floor(x); }
static inline float amix_fabsf(float x) { return x < 0 ? -x : x; }
static inline float amix_sqrtf(float x) { return (float)sqrt(x); }
static inline long amix_lroundf(float x)
{
	return x < 0 ? -(long)(0.5f - x) : (long)(x + 0.5f);
}
#define ceilf amix_ceilf
#define floorf amix_floorf
#define fabsf amix_fabsf
#define sqrtf amix_sqrtf
#define lroundf amix_lroundf
#ifndef isnan
#define isnan __builtin_isnan
#endif

#endif
