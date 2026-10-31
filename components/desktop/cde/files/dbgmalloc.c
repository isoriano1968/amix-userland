/*
 * dbgmalloc.c -- a checking malloc for hunting heap corruption on AMIX.
 * Link it into a program (before -lc) and it replaces libc's malloc family,
 * libc.so's own calls included.  Bump allocation from a static arena, never
 * reused; every block has a header (size, allocating caller, magic) and a
 * trailing guard; freed blocks are filled with 0xdd.  Every 64 calls -- and
 * on each free -- the blocks are checked, and the first damage found is
 * reported on stderr with the block's size and allocating caller, then the
 * program aborts.  DBGMALLOC_EVERY=n checks every n calls.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define ARENA (6u << 20)
#define MAXBLK 60000
#define MAGIC 0x4d414c4cUL	/* MALL */
#define GUARD 0xa5a5a5a5UL
#define TAIL 8

struct hdr { uint32_t magic, size, site, freed; };
static char arena[ARENA] __attribute__((aligned(16)));
static size_t used;
static struct hdr *blk[MAXBLK];
static int nblk, ncalls, every = 64;

static void out(const char *s) { write(2, s, strlen(s)); }
static void hex(uint32_t v)
{
	char b[11] = "0x00000000";
	int i;
	for (i = 9; i > 1; i--, v >>= 4)
		b[i] = "0123456789abcdef"[v & 15];
	out(b);
}

static void damage(const char *what, struct hdr *h, uint32_t where)
{
	out("dbgmalloc: ");
	out(what);
	out(" block ");
	hex((uint32_t)(uintptr_t)(h + 1));
	out(" size ");
	hex(h->size);
	out(" allocated by ");
	hex(h->site);
	out(h->freed ? " (freed)" : "");
	out(", found at call from ");
	hex(where);
	out("\n");
	abort();
}

static void check1(struct hdr *h, uint32_t where)
{
	unsigned char *p = (unsigned char *)(h + 1), *t = p + h->size;
	uint32_t i;

	if (h->magic != MAGIC)
		damage("header overwritten:", h, where);
	for (i = 0; i < TAIL; i++)
		if (t[i] != 0xa5)
			damage("written past the end:", h, where);
	if (h->freed)
		for (i = 0; i < h->size; i++)
			if (p[i] != 0xdd)
				damage("written after free:", h, where);
}

static void checkall(uint32_t where)
{
	int i;
	for (i = 0; i < nblk; i++)
		check1(blk[i], where);
}

static void tick(uint32_t where)
{
	if (!ncalls) {
		char *e = getenv("DBGMALLOC_EVERY");
		if (e && atoi(e) > 0)
			every = atoi(e);
	}
	if (++ncalls % every == 0)
		checkall(where);
}

static void *alloc(size_t n, uint32_t site)
{
	size_t need = (sizeof (struct hdr) + n + TAIL + 15) & ~(size_t)15;
	struct hdr *h;

	tick(site);
	if (used + need > ARENA || nblk >= MAXBLK) {
		out("dbgmalloc: arena full\n");
		abort();
	}
	h = (struct hdr *)(arena + used);
	used += need;
	h->magic = MAGIC;
	h->size = n;
	h->site = site;
	h->freed = 0;
	memset((char *)(h + 1) + n, 0xa5, TAIL);
	blk[nblk++] = h;
	return h + 1;
}

static struct hdr *hdrof(void *p, uint32_t where)
{
	struct hdr *h = (struct hdr *)p - 1;
	if ((char *)p < arena || (char *)p >= arena + ARENA) {
		out("dbgmalloc: free/realloc of a pointer not from malloc: ");
		hex((uint32_t)(uintptr_t)p);
		out(" from ");
		hex(where);
		out("\n");
		abort();
	}
	if (h->magic != MAGIC)
		damage("bad pointer or header overwritten:", h, where);
	return h;
}

/* The entry points are dbg_*; the assembly wrappers below are what callers
** reach as malloc/calloc/realloc.  gcc 14 returns pointers in %d0 only, the
** AMIX (SVR4 m68k) convention -- and gcc 2, which built the X11R6.3
** libraries -- is %a0 with a copy in %d0, and gcc-2 code reads %a0.  The
** caller's address is passed in, as the wrapper is not the caller. */
void *dbg_malloc(size_t n, uint32_t site) { return alloc(n, site); }

void *dbg_calloc(size_t a, size_t b, uint32_t site)
{
	void *p = alloc(a * b, site);
	memset(p, 0, a * b);
	return p;
}

void free(void *p)
{
	uint32_t where = (uint32_t)(uintptr_t)__builtin_return_address(0);
	struct hdr *h;

	if (!p)
		return;
	h = hdrof(p, where);
	if (h->freed) {
		out("dbgmalloc: double free, ");
		damage("", h, where);
	}
	check1(h, where);
	h->freed = 1;
	memset(p, 0xdd, h->size);
	tick(where);
}

void *dbg_realloc(void *p, size_t n, uint32_t where)
{
	struct hdr *h;
	void *q;

	if (!p)
		return alloc(n, where);
	h = hdrof(p, where);
	check1(h, where);
	q = alloc(n, where);
	memcpy(q, p, h->size < n ? h->size : n);
	h->freed = 1;
	memset(p, 0xdd, h->size);
	return q;
}

/* malloc/calloc/realloc: call dbg_* with the caller's return address as the
** extra argument, then return the result in %a0 as well as %d0 */
__asm__(
"	.globl	malloc\n"
"malloc:	move.l	(%sp),-(%sp)\n"		/* site = our return address */
"	move.l	8(%sp),-(%sp)\n"			/* n */
"	jsr	dbg_malloc\n"
"	addq.l	#8,%sp\n"
"	move.l	%d0,%a0\n"
"	rts\n"
"	.globl	calloc\n"
"calloc:	move.l	(%sp),-(%sp)\n"
"	move.l	12(%sp),-(%sp)\n"			/* b */
"	move.l	12(%sp),-(%sp)\n"			/* a */
"	jsr	dbg_calloc\n"
"	lea	12(%sp),%sp\n"
"	move.l	%d0,%a0\n"
"	rts\n"
"	.globl	realloc\n"
"realloc:	move.l	(%sp),-(%sp)\n"
"	move.l	12(%sp),-(%sp)\n"			/* n */
"	move.l	12(%sp),-(%sp)\n"			/* p */
"	jsr	dbg_realloc\n"
"	lea	12(%sp),%sp\n"
"	move.l	%d0,%a0\n"
"	rts\n");
