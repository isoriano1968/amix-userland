/* amix-grafx2.h -- forced into every Grafx2 file: what AMIX's libc lacks */
#ifndef AMIX_GRAFX2_H
#define AMIX_GRAFX2_H
/* C99's round(): halves away from zero */
static inline double amix_round(double x) { return x < 0 ? -(double)(long)(0.5 - x) : (double)(long)(x + 0.5); }
#define round(x) amix_round(x)
#endif
