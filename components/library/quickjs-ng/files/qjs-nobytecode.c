/* qjs-nobytecode.c -- qjs on AMIX without its built-in REPL and standalone
 * bytecode: gen/repl.c and gen/standalone.c come from a little-endian
 * qjsc, and QuickJS reads bytecode in the machine's own byte order only.
 * qjs FILE works; the REPL and -c report a read error. */
#include <stdint.h>

const uint8_t qjsc_repl[1];
const uint32_t qjsc_repl_size = 0;
const uint8_t qjsc_standalone[1];
const uint32_t qjsc_standalone_size = 0;
