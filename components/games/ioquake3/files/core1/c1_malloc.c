/*
 * c1_malloc.c -- newlib's reentrant allocation entry points (its stdio,
 * strdup ...) onto dlmalloc 2.8.6 (library/dlmalloc, compiled by the
 * Makefile with a non-contiguous MORECORE: core1_main.c's c1p_morecore
 * hands out the heap region below the image and the one above it, and
 * dlmalloc keeps them as separate segments).  newlib's own malloc (2.6)
 * takes no memory below its old top and lost the rest of the low region.
 */
#include <malloc.h>
#include <reent.h>

void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_calloc_r(struct _reent *r, size_t n, size_t s) { (void)r; return calloc(n, s); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }
void *_memalign_r(struct _reent *r, size_t a, size_t n) { (void)r; return memalign(a, n); }
void *_valloc_r(struct _reent *r, size_t n) { (void)r; return valloc(n); }
void *_pvalloc_r(struct _reent *r, size_t n) { (void)r; return pvalloc(n); }
size_t _malloc_usable_size_r(struct _reent *r, void *p) { (void)r; return malloc_usable_size(p); }
int _malloc_trim_r(struct _reent *r, size_t pad) { (void)r; return malloc_trim(pad); }
struct mallinfo _mallinfo_r(struct _reent *r) { (void)r; return mallinfo(); }
void _malloc_stats_r(struct _reent *r) { (void)r; }
