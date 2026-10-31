/*
 * amix-c99.h -- the C99 bits pt2-clone uses that AMIX's 1991 libc/libm lack
 * (included into every pt2-clone source by build.sh with -include).
 */
#ifndef AMIX_C99_H
#define AMIX_C99_H
#include <stddef.h>
#include <string.h>
#include <math.h>

static __inline__ double amix_round(double x) { return x < 0.0 ? -floor(-x + 0.5) : floor(x + 0.5); }
static __inline__ float amix_roundf(float x) { return (float)amix_round(x); }
static __inline__ double amix_exp2(double x) { return pow(2.0, x); }
static __inline__ double amix_log2(double x) { return log(x) * 1.4426950408889634074; }
#define round(x)  amix_round(x)
#define roundf(x) amix_roundf(x)
#define exp2(x)   amix_exp2(x)
#define log2(x)   amix_log2(x)
#endif
