/* m16fill.c -- is a move16 fill of cached RAM coherent, and how fast is it?
 * (2026-10-26)  Before Mesa clears its back buffer with move16: the buffer
 * is ordinary cacheable memory the CPU has just read, so the data cache
 * holds the old lines.  If move16 left them there, the next read would see
 * the old pixels.  Each round reads the whole buffer (cache warm with the
 * old value), fills it, then reads it back and counts wrong longs.
 *
 *   m16fill           512x320 16-bit (327680 bytes), long loop vs move16
 *
 * Build (gcc14-cross-amix): m68k-elf-gcc -O2 -m68060 -o m16fill m16fill.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define BYTES   (512 * 2 * 320)
#define REPS    20

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static unsigned long line[16 + 4];      /* 64 bytes, 16-byte aligned inside */

static void fill_long(unsigned long *p, long n, unsigned long v)
{
    while (n-- > 0)
        *p++ = v;
}

static void fill_move16(unsigned long *p, long n, unsigned long v)
{
    unsigned long *src = (unsigned long *)(((unsigned long) line + 15) & ~15UL);
    unsigned long *s;
    int i;

    for (i = 0; i < 16; i++)
        src[i] = v;
    for (n >>= 4; n > 0; n--) {         /* 64 bytes per step */
        s = src;
        __asm__ volatile("move16 (%0)+,(%1)+\n\t"
                         "move16 (%0)+,(%1)+\n\t"
                         "move16 (%0)+,(%1)+\n\t"
                         "move16 (%0)+,(%1)+"
                         : "+a"(s), "+a"(p) : : "memory");
    }
}

static volatile unsigned long sink;

static long check(unsigned long *p, long n, unsigned long v)
{
    long bad = 0;
    while (n-- > 0)
        if (*p++ != v)
            bad++;
    return bad;
}

static void warm(unsigned long *p, long n)
{
    unsigned long s = 0;
    while (n-- > 0)
        s += *p++;
    sink = s;
}

int main(void)
{
    char *raw = malloc(BYTES + 16);
    unsigned long *buf = (unsigned long *)(((unsigned long) raw + 15) & ~15UL);
    long n = BYTES / 4, bad = 0;
    double t, tl = 0, tm = 0;
    int r;

    for (r = 0; r < REPS; r++) {
        unsigned long a = 0x11110000UL + r, b = 0x22220000UL + r;

        fill_long(buf, n, a);
        warm(buf, n);
        t = now(); fill_move16(buf, n, b); tm += now() - t;
        bad += check(buf, n, b);        /* stale cache lines would show a */
        warm(buf, n);
        t = now(); fill_long(buf, n, a); tl += now() - t;
        bad += check(buf, n, a);
        /* a small buffer that fits the 8 KB data cache entirely */
        fill_long(buf, 1024, a);
        warm(buf, 1024);
        fill_move16(buf, 1024, b);
        bad += check(buf, 1024, b);
    }
    printf("long fill   %6.2f ms per 512x320\n", tl / REPS);
    printf("move16 fill %6.2f ms per 512x320\n", tm / REPS);
    printf("wrong longs after move16 over warm cache: %ld\n", bad);
    return bad != 0;
}
