/*
 * amix-mem.c -- memcpy/memmove/memset/memcmp for pt2-clone on AMIX.
 * AMIX's 1991 libc versions work a byte at a time: 9 MB/s copying and
 * 4.4 MB/s comparing on the Z3660's 68060, where 32-bit loops reach the
 * memory's ~17 MB/s and 64 MB/s inside the data cache.  Linking these into
 * the program takes them in place of libc.so's.
 * Built with -fno-builtin -fno-tree-loop-distribute-patterns, or gcc turns
 * the loops back into calls to themselves.
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n && ((uintptr_t)d & 3)) {
		*d++ = *s++;
		n--;
	}
	{	/* the 060 handles a misaligned source in hardware */
		uint32_t *dl = (uint32_t *)d;
		const uint32_t *sl = (const uint32_t *)s;
		size_t k;

		for (k = n >> 5; k; k--) {
			uint32_t a = sl[0], b = sl[1], c = sl[2], e = sl[3];
			dl[0] = a; dl[1] = b; dl[2] = c; dl[3] = e;
			a = sl[4]; b = sl[5]; c = sl[6]; e = sl[7];
			dl[4] = a; dl[5] = b; dl[6] = c; dl[7] = e;
			dl += 8;
			sl += 8;
		}
		for (k = (n >> 2) & 7; k; k--)
			*dl++ = *sl++;
		d = (unsigned char *)dl;
		s = (const unsigned char *)sl;
	}
	for (n &= 3; n; n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d <= s || d >= s + n)
		return memcpy(dst, src, n);
	d += n;			/* overlapping, destination above: copy backwards */
	s += n;
	while (n && ((uintptr_t)d & 3)) {
		*--d = *--s;
		n--;
	}
	{
		uint32_t *dl = (uint32_t *)d;
		const uint32_t *sl = (const uint32_t *)s;
		size_t k;

		for (k = n >> 2; k; k--)
			*--dl = *--sl;
		d = (unsigned char *)dl;
		s = (const unsigned char *)sl;
	}
	for (n &= 3; n; n--)
		*--d = *--s;
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;
	uint32_t v = (unsigned char)c;

	v |= v << 8;
	v |= v << 16;
	while (n && ((uintptr_t)d & 3)) {
		*d++ = (unsigned char)c;
		n--;
	}
	{
		uint32_t *dl = (uint32_t *)d;
		size_t k;

		for (k = n >> 5; k; k--) {
			dl[0] = v; dl[1] = v; dl[2] = v; dl[3] = v;
			dl[4] = v; dl[5] = v; dl[6] = v; dl[7] = v;
			dl += 8;
		}
		for (k = (n >> 2) & 7; k; k--)
			*dl++ = v;
		d = (unsigned char *)dl;
	}
	for (n &= 3; n; n--)
		*d++ = (unsigned char)c;
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a, *q = b;

	if (!(((uintptr_t)p | (uintptr_t)q) & 3)) {
		const uint32_t *pl = (const uint32_t *)p, *ql = (const uint32_t *)q;

		while (n >= 4 && *pl == *ql) {
			pl++;
			ql++;
			n -= 4;
		}
		p = (const unsigned char *)pl;
		q = (const unsigned char *)ql;
	}
	for (; n; n--, p++, q++)
		if (*p != *q)
			return *p - *q;
	return 0;
}
