/*
 * core_portme.c -- CoreMark's platform for c1bench.adf (see core_portme.h).
 */
#include <stdarg.h>
#include "coremark.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
ee_u32 default_num_contexts = 1;

#define MARK(v)  (*(volatile ee_u32 *)C1BENCH_MARK = (v))

void start_time(void)
{
    MARK(0x43314253);           /* "C1BS" */
}

void stop_time(void)
{
    MARK(0x43314245);           /* "C1BE" */
}

/* the host has the real time; ten "seconds" keep CoreMark's own sums sane */
#define EE_TICKS_PER_SEC 1000
CORE_TICKS get_time(void)
{
    return 10 * EE_TICKS_PER_SEC;
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    return ticks / EE_TICKS_PER_SEC;
}

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc; (void)argv;
    p->portable_id = 1;
    *(volatile char *)C1BENCH_TEXT = 0;
    *(volatile ee_u32 *)C1BENCH_ITER = ITERATIONS;
}

void portable_fini(core_portable *p)
{
    p->portable_id = 0;
}

/* a small printf into chip RAM: %d %u %x %s %c %ld %lu %lx, widths ignored */
static char *out = (char *)C1BENCH_TEXT;

static void put(char c)
{
    if (out < (char *)C1BENCH_TEXT + 0x1E00) {
        *out++ = c;
        *out = 0;
    }
}

static void num(unsigned long v, int base, int neg)
{
    char b[12];
    int n = 0;

    if (neg)
        put('-');
    do {
        b[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (n)
        put(b[--n]);
}

int ee_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(*fmt);
            continue;
        }
        fmt++;
        while ((*fmt >= '0' && *fmt <= '9') || *fmt == '-' || *fmt == '.')
            fmt++;
        if (*fmt == 'l')
            fmt++;
        switch (*fmt) {
        case 'd': { long v = va_arg(ap, long); num(v < 0 ? -v : v, 10, v < 0); break; }
        case 'u': num(va_arg(ap, unsigned long), 10, 0); break;
        case 'x': case 'X': num(va_arg(ap, unsigned long), 16, 0); break;
        case 's': { const char *s = va_arg(ap, const char *); while (*s) put(*s++); break; }
        case 'c': put((char)va_arg(ap, int)); break;
        case 'f': (void)va_arg(ap, double); put('?'); break;
        default: put(*fmt);
        }
    }
    va_end(ap);
    return 0;
}
