/* math.h -- add the C99 classification macros AMIX's <math.h> predates.
 *
 * This is a FIXED header in GCC's sense: it does not replace the target's
 * <math.h>, it reaches it with #include_next and adds what eight years of
 * standardisation added afterwards. GCC searches include-fixed before the
 * sysroot precisely so that an incomplete system header can be repaired
 * without touching the vendor's tree.
 *
 * What needed repairing: libstdc++'s C++17 floating-point to_chars does
 *
 *     switch (__builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL,
 *                                  FP_SUBNORMAL, FP_ZERO, value))
 *
 * and without the macros the whole library fails to build. Note what that
 * builtin means for us: GCC does the classification itself and takes these
 * only as the tags to return, so they need no implementation behind them and
 * only have to be distinct. The values are glibc's, so that anything sharing
 * a compiled object with a glibc-built library still agrees.
 */
#ifndef AMIX_FIXED_MATH_H
#define AMIX_FIXED_MATH_H

#include_next <math.h>

#ifndef FP_NAN
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#endif

#endif /* AMIX_FIXED_MATH_H */
