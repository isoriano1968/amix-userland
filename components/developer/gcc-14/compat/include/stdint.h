/* stdint.h -- C99 exact-width integer types for AMIX.
 *
 * AMIX is SVR4.0, from 1991, and C99 is eight years in its future: there is no
 * <stdint.h> anywhere in /usr/include. That stops more than portable
 * application code -- GCC's own <stdint.h> for this target is a WRAPPER that
 * does #include_next, expecting the C library to supply the real one, so
 * libstdc++ itself will not build without this file.
 *
 * Every type below is spelled with GCC's own predefined macros rather than a
 * hardcoded "long" or "int". That is deliberate: it cannot disagree with the
 * compiler about what a 64-bit integer is on this ABI, and it stays correct if
 * the CPU or the flags change. A hand-written version that guessed wrong here
 * would produce a program that compiles and then computes the wrong answers.
 *
 * This file is OURS, not AMIX's -- it is installed into the sysroot alongside
 * the vendor headers by tools/sync-sysroot.sh, and carries no AT&T or
 * Commodore code.
 */
#ifndef _STDINT_H
#define _STDINT_H

/* Exact-width types. */
typedef __INT8_TYPE__    int8_t;
typedef __INT16_TYPE__   int16_t;
typedef __INT32_TYPE__   int32_t;
typedef __INT64_TYPE__   int64_t;
typedef __UINT8_TYPE__   uint8_t;
typedef __UINT16_TYPE__  uint16_t;
typedef __UINT32_TYPE__  uint32_t;
typedef __UINT64_TYPE__  uint64_t;

/* Minimum-width types. */
typedef __INT_LEAST8_TYPE__   int_least8_t;
typedef __INT_LEAST16_TYPE__  int_least16_t;
typedef __INT_LEAST32_TYPE__  int_least32_t;
typedef __INT_LEAST64_TYPE__  int_least64_t;
typedef __UINT_LEAST8_TYPE__  uint_least8_t;
typedef __UINT_LEAST16_TYPE__ uint_least16_t;
typedef __UINT_LEAST32_TYPE__ uint_least32_t;
typedef __UINT_LEAST64_TYPE__ uint_least64_t;

/* Fastest minimum-width types. */
typedef __INT_FAST8_TYPE__    int_fast8_t;
typedef __INT_FAST16_TYPE__   int_fast16_t;
typedef __INT_FAST32_TYPE__   int_fast32_t;
typedef __INT_FAST64_TYPE__   int_fast64_t;
typedef __UINT_FAST8_TYPE__   uint_fast8_t;
typedef __UINT_FAST16_TYPE__  uint_fast16_t;
typedef __UINT_FAST32_TYPE__  uint_fast32_t;
typedef __UINT_FAST64_TYPE__  uint_fast64_t;

/* Integers wide enough to hold a pointer, and the widest integers there are. */
typedef __INTPTR_TYPE__   intptr_t;
typedef __UINTPTR_TYPE__  uintptr_t;
typedef __INTMAX_TYPE__   intmax_t;
typedef __UINTMAX_TYPE__  uintmax_t;

/* Limits. */
#define INT8_MIN    (-__INT8_MAX__ - 1)
#define INT16_MIN   (-__INT16_MAX__ - 1)
#define INT32_MIN   (-__INT32_MAX__ - 1)
#define INT64_MIN   (-__INT64_MAX__ - 1)
#define INT8_MAX    __INT8_MAX__
#define INT16_MAX   __INT16_MAX__
#define INT32_MAX   __INT32_MAX__
#define INT64_MAX   __INT64_MAX__
#define UINT8_MAX   __UINT8_MAX__
#define UINT16_MAX  __UINT16_MAX__
#define UINT32_MAX  __UINT32_MAX__
#define UINT64_MAX  __UINT64_MAX__

#define INT_LEAST8_MIN   (-__INT_LEAST8_MAX__ - 1)
#define INT_LEAST16_MIN  (-__INT_LEAST16_MAX__ - 1)
#define INT_LEAST32_MIN  (-__INT_LEAST32_MAX__ - 1)
#define INT_LEAST64_MIN  (-__INT_LEAST64_MAX__ - 1)
#define INT_LEAST8_MAX   __INT_LEAST8_MAX__
#define INT_LEAST16_MAX  __INT_LEAST16_MAX__
#define INT_LEAST32_MAX  __INT_LEAST32_MAX__
#define INT_LEAST64_MAX  __INT_LEAST64_MAX__
#define UINT_LEAST8_MAX  __UINT_LEAST8_MAX__
#define UINT_LEAST16_MAX __UINT_LEAST16_MAX__
#define UINT_LEAST32_MAX __UINT_LEAST32_MAX__
#define UINT_LEAST64_MAX __UINT_LEAST64_MAX__

#define INT_FAST8_MIN   (-__INT_FAST8_MAX__ - 1)
#define INT_FAST16_MIN  (-__INT_FAST16_MAX__ - 1)
#define INT_FAST32_MIN  (-__INT_FAST32_MAX__ - 1)
#define INT_FAST64_MIN  (-__INT_FAST64_MAX__ - 1)
#define INT_FAST8_MAX   __INT_FAST8_MAX__
#define INT_FAST16_MAX  __INT_FAST16_MAX__
#define INT_FAST32_MAX  __INT_FAST32_MAX__
#define INT_FAST64_MAX  __INT_FAST64_MAX__
#define UINT_FAST8_MAX  __UINT_FAST8_MAX__
#define UINT_FAST16_MAX __UINT_FAST16_MAX__
#define UINT_FAST32_MAX __UINT_FAST32_MAX__
#define UINT_FAST64_MAX __UINT_FAST64_MAX__

#define INTPTR_MIN   (-__INTPTR_MAX__ - 1)
#define INTPTR_MAX   __INTPTR_MAX__
#define UINTPTR_MAX  __UINTPTR_MAX__
#define INTMAX_MIN   (-__INTMAX_MAX__ - 1)
#define INTMAX_MAX   __INTMAX_MAX__
#define UINTMAX_MAX  __UINTMAX_MAX__
#define PTRDIFF_MIN  (-__PTRDIFF_MAX__ - 1)
#define PTRDIFF_MAX  __PTRDIFF_MAX__
#define SIZE_MAX     __SIZE_MAX__
#ifdef __WCHAR_MAX__
#define WCHAR_MAX    __WCHAR_MAX__
#endif
#ifdef __WCHAR_MIN__
#define WCHAR_MIN    __WCHAR_MIN__
#endif
#define WINT_MIN     (-__INT32_MAX__ - 1)
#define WINT_MAX     __INT32_MAX__

#define SIG_ATOMIC_MIN  (-__INT32_MAX__ - 1)
#define SIG_ATOMIC_MAX  __INT32_MAX__

/* Constant-suffix macros. */
#define INT8_C(c)    c
#define INT16_C(c)   c
#define INT32_C(c)   __INT32_C(c)
#define INT64_C(c)   __INT64_C(c)
#define UINT8_C(c)   c
#define UINT16_C(c)  c
#define UINT32_C(c)  __UINT32_C(c)
#define UINT64_C(c)  __UINT64_C(c)
#define INTMAX_C(c)  __INTMAX_C(c)
#define UINTMAX_C(c) __UINTMAX_C(c)

#endif /* _STDINT_H */
