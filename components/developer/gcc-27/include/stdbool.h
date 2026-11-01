/* stdbool.h for AMIX: gcc 2.7 lacks it; a newer gcc has its own */
#if defined(__GNUC__) && __GNUC__ >= 3
#include_next <stdbool.h>
#else
#ifndef _STDBOOL_H
#define _STDBOOL_H
#define bool _Bool
typedef unsigned char _Bool;
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif
#endif
