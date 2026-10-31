/* inttypes.h -- AMIX (SVR4.0) has none; gcc14-cross-amix provides <stdint.h>.
 * Without this, fileutils' configure #defines uintmax_t, which then clashes
 * with stdint.h's typedef. */
#ifndef AMIX_INTTYPES_H
#define AMIX_INTTYPES_H
#include <stdint.h>
#endif
