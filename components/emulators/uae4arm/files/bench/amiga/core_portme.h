/*
 * core_portme.h -- CoreMark on an Amiga booted from c1bench.adf (no OS
 * once it runs: the boot block loads it to $70000 and disables
 * interrupts).  The time is taken outside: start_time and stop_time write
 * marks into chip RAM, and the emulator's host (z3660ARM_Core_uae, or the
 * PC test) notes its own clock when they appear.  ee_printf writes the
 * report into chip RAM for the host to show.
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H
#include <stddef.h>
#define HAS_FLOAT 0
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 0
#define HAS_PRINTF 0
#define COMPILER_VERSION "GCC" __VERSION__
#define COMPILER_FLAGS "-O2 -m68000"
#define MEM_LOCATION "STATIC"
typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef int ee_f32;
typedef unsigned char ee_u8;
typedef unsigned int ee_u32;
typedef ee_u32 ee_ptr_int;
typedef size_t ee_size_t;
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x) - 1) & ~3))
#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;
#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0
extern ee_u32 default_num_contexts;
typedef struct CORE_PORTABLE_S { ee_u8 portable_id; } core_portable;
void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);
#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif
int ee_printf(const char *fmt, ...);

/* the marks, read by the host (big-endian longs in chip RAM) */
#define C1BENCH_MARK    0x0007FF00UL    /* "C1BS" started, "C1BE" stopped, "C1BD" done */
#define C1BENCH_ITER    0x0007FF04UL    /* iterations */
#define C1BENCH_CRC     0x0007FF08UL    /* crcfinal */
#define C1BENCH_TEXT    0x0007E000UL    /* the report, NUL-ended, 7.5 KB */
#endif
