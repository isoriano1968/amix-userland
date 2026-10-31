/*
 * inttypes.h for AMIX with gcc14-cross-amix, which has <stdint.h> but no
 * <inttypes.h>.  The format macros follow that <stdint.h>: int32_t and
 * intptr_t are long, int64_t and intmax_t are long long.  64-bit
 * conversions go through the toolchain's own snprintf/vsnprintf; AMIX's
 * printf/fprintf mis-handle "ll".
 */
#ifndef AMIX_INTTYPES_H
#define AMIX_INTTYPES_H

#include <stdint.h>

#define PRId8	"d"
#define PRIi8	"i"
#define PRIu8	"u"
#define PRIx8	"x"
#define PRIX8	"X"
#define PRId16	"d"
#define PRIi16	"i"
#define PRIu16	"u"
#define PRIx16	"x"
#define PRIX16	"X"
#define PRId32	"ld"
#define PRIi32	"li"
#define PRIu32	"lu"
#define PRIx32	"lx"
#define PRIX32	"lX"
#define PRIo32	"lo"
#define PRId64	"lld"
#define PRIi64	"lli"
#define PRIu64	"llu"
#define PRIx64	"llx"
#define PRIX64	"llX"
#define PRIo64	"llo"
#define PRIdMAX	"lld"
#define PRIuMAX	"llu"
#define PRIxMAX	"llx"
#define PRIdPTR	"ld"
#define PRIuPTR	"lu"
#define PRIxPTR	"lx"
#define SCNd32	"ld"
#define SCNu32	"lu"
#define SCNx32	"lx"
#define SCNd64	"lld"
#define SCNu64	"llu"
#define SCNx64	"llx"

typedef struct { intmax_t quot, rem; } imaxdiv_t;

long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
#define strtoimax(s, e, b)	strtoll(s, e, b)
#define strtoumax(s, e, b)	strtoull(s, e, b)

#endif
