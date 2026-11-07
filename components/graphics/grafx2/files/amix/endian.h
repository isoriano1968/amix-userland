/* endian.h for Grafx2 on AMIX: m68k is big-endian, and AMIX has no endian.h */
#ifndef AMIX_ENDIAN_H
#define AMIX_ENDIAN_H
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN    4321
#define BYTE_ORDER    BIG_ENDIAN
#define be16toh(x) (x)
#define be32toh(x) (x)
#define htobe16(x) (x)
#define htobe32(x) (x)
#endif
