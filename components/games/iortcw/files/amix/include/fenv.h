/* fenv.h -- AMIX has none; ioquake3 only sets the default rounding */
#ifndef AMIX_FENV_H
#define AMIX_FENV_H
#define FE_TONEAREST 0
#define FE_DOWNWARD 1
#define FE_UPWARD 2
#define FE_TOWARDZERO 3
static __inline__ int fesetround(int r) { (void) r; return 0; }
static __inline__ int fegetround(void) { return FE_TONEAREST; }
#endif
