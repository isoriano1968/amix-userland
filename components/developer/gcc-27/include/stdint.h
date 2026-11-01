/* stdint.h for AMIX: gcc 2.7 lacks it and current headers such as ncurses.h want it; a newer gcc has its own */
#if defined(__GNUC__) && __GNUC__ >= 3
#include_next <stdint.h>
#else
#ifndef _STDINT_H
#define _STDINT_H
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
#endif
#endif
