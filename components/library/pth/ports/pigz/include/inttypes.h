/* Pigz-local shim: it uses integer typedefs, not inttypes format/scan macros.
 * The GCC 14 AMIX toolchain supplies those typedefs in its own stdint.h.
 * This is not a complete inttypes.h and must not be installed system-wide.
 */
#ifndef AMIX_PIGZ_INTTYPES_H
#define AMIX_PIGZ_INTTYPES_H
#include <stdint.h>
#endif
