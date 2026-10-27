/*
 * mem060.c -- memcpy, memmove, memset, memcmp for AMIX on the 68060
 * (2026-10-26).  AMIX's libc does these a byte at a time (memcpy ~9 MB/s,
 * memcmp ~4.4 MB/s); gcc also turns every larger structure copy into a
 * memcpy call, so TinyGL's ~150-byte GLVertex copies cost ~17 us each.
 * These move longs, four to a loop pass; the 060 does misaligned long
 * accesses in hardware, so only the destination is aligned first.
 *
 * Linked into an executable they take its own calls (libc.so keeps its
 * own).  Compiled with -fno-builtin -fno-tree-loop-distribute-patterns
 * (Makefile), or gcc would turn the loops back into calls to themselves.
 */
#include <stddef.h>

typedef unsigned long ul;

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (n >= 16) {
        while ((ul) d & 3) {
            *d++ = *s++;
            n--;
        }
        {
            ul *dl = (ul *) d;
            const ul *sl = (const ul *) s;
            size_t k = n >> 4;
            while (k--) {
                dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3];
                dl += 4; sl += 4;
            }
            n &= 15;
            while (n >= 4) {
                *dl++ = *sl++;
                n -= 4;
            }
            d = (unsigned char *) dl;
            s = (const unsigned char *) sl;
        }
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d <= s || d >= s + n)
        return memcpy(dst, src, n);
    d += n;                                 /* overlapping, upwards: from the end */
    s += n;
    if (n >= 16) {
        while ((ul) d & 3) {
            *--d = *--s;
            n--;
        }
        {
            ul *dl = (ul *) d;
            const ul *sl = (const ul *) s;
            while (n >= 4) {
                *--dl = *--sl;
                n -= 4;
            }
            d = (unsigned char *) dl;
            s = (const unsigned char *) sl;
        }
    }
    while (n--)
        *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;

    if (n >= 16) {
        ul v = (unsigned char) c;
        v |= v << 8;
        v |= v << 16;
        while ((ul) d & 3) {
            *d++ = (unsigned char) c;
            n--;
        }
        {
            ul *dl = (ul *) d;
            size_t k = n >> 4;
            while (k--) {
                dl[0] = v; dl[1] = v; dl[2] = v; dl[3] = v;
                dl += 4;
            }
            n &= 15;
            while (n >= 4) {
                *dl++ = v;
                n -= 4;
            }
            d = (unsigned char *) dl;
        }
    }
    while (n--)
        *d++ = (unsigned char) c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;

    while (n >= 4 && *(const ul *) p == *(const ul *) q) {
        p += 4;
        q += 4;
        n -= 4;
    }
    while (n--) {
        if (*p != *q)
            return *p - *q;
        p++;
        q++;
    }
    return 0;
}
