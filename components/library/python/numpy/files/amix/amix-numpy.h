/* amix-numpy.h -- forced into every numpy file (C and C++), after the
 * compiler's own headers are set up: what AMIX's SVR4.0 libc and the gcc 14
 * cross toolchain's libstdc++ (built without C99 support for the target)
 * lack.  Static inline only, so that nothing here is exported by a module.
 * gcc 14 only: numpy needs C11 and C++17, which AMIX's gcc 2.7 has not. */
#ifndef AMIX_NUMPY_H
#define AMIX_NUMPY_H

#ifdef __cplusplus
/* Python's compatibility declarations are C; in C++ they must keep C
 * linkage, and must come before pyconfig.h (it #defines socklen_t, which
 * Pth's pthread.h, included by them, typedefs). */
extern "C" {
#include <amix-python.h>
}
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* C99 llabs: not in SVR4.0's libc */
static inline long long amix_llabs(long long x) { return x < 0 ? -x : x; }
#define llabs(x) amix_llabs(x)

/* wmemchr: AMIX has no wchar.h functions (wchar_t is 4 bytes, UCS-4) */
static inline wchar_t *amix_wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
	for (; n; n--, s++)
		if (*s == c)
			return (wchar_t *)s;
	return NULL;
}
#define wmemchr(s, c, n) amix_wmemchr(s, c, n)

#ifdef __cplusplus
/* AMIX's stdio.h and ctype.h define getchar, isalpha ... as macros, which
 * break numpy's C++ members of those names; <cstdio> and <cctype> take
 * them back. */
#include <cstdio>
#include <cctype>
#include <cmath>
/* The cross libstdc++ was configured without _GLIBCXX_USE_C99_MATH, so
 * <cmath> removes C99's classification macros and puts nothing in std;
 * gcc's builtins in their place. */
#undef isnan
#undef isinf
#undef isfinite
#undef signbit
#undef isnormal
#undef isless
#undef islessequal
#undef isgreater
#undef isgreaterequal
#undef isunordered
namespace std {
#define AMIX_STD_CLASSIFY(name, builtin) \
	inline bool name(float x) { return builtin(x); } \
	inline bool name(double x) { return builtin(x); } \
	inline bool name(long double x) { return builtin(x); }
AMIX_STD_CLASSIFY(isnan, __builtin_isnan)
AMIX_STD_CLASSIFY(isinf, __builtin_isinf)
AMIX_STD_CLASSIFY(isfinite, __builtin_isfinite)
AMIX_STD_CLASSIFY(signbit, __builtin_signbit)
AMIX_STD_CLASSIFY(isnormal, __builtin_isnormal)
#undef AMIX_STD_CLASSIFY
#define AMIX_STD_COMPARE(name, builtin) \
	inline bool name(float x, float y) { return builtin(x, y); } \
	inline bool name(double x, double y) { return builtin(x, y); } \
	inline bool name(long double x, long double y) { return builtin(x, y); }
AMIX_STD_COMPARE(isless, __builtin_isless)
AMIX_STD_COMPARE(islessequal, __builtin_islessequal)
AMIX_STD_COMPARE(isgreater, __builtin_isgreater)
AMIX_STD_COMPARE(isgreaterequal, __builtin_isgreaterequal)
AMIX_STD_COMPARE(isunordered, __builtin_isunordered)
#undef AMIX_STD_COMPARE
}
/* and for C-style calls in C++ files (AMIX's math.h has its own
 * int isnan(double) already) */
using std::isinf;
using std::isfinite;
using std::signbit;
using std::isnormal;
using std::isless;
using std::islessequal;
using std::isgreater;
using std::isgreaterequal;
using std::isunordered;
#endif

#endif
