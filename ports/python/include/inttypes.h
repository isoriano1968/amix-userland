#ifndef AMIX_PYTHON_INTTYPES_H
#define AMIX_PYTHON_INTTYPES_H
/* CPython's private format-macro subset for GCC 14's 32-bit AMIX ABI.
 * This is not a complete system inttypes.h. */
#include <stdint.h>
#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIu64 "llu"
#define PRIi32 "li"
#define PRIu32 "lu"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#endif
