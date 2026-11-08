/*
 * huffcheck.c -- the tree huffrestore.h rebuilds from huffinit.h must equal
 * the one Huff_addRef builds from msg_hData, node for node (see huffgen.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "q_shared.h"
#include "qcommon.h"

#ifndef Com_Memset
void Com_Memset(void *d, const int c, size_t n) { memset(d, c, n); }
void Com_Memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
#endif

#include HDATA
#include "../huffrestore.h"

static huffman_t built;
static huffman_t restored;

static int ni(const huff_t *h, const node_t *p)
{
	return p ? (int)(p - h->nodeList) : -1;
}

/* a nodePtrs entry: a node (index into nodeList), or on the free list a
   pointer into nodePtrs itself (huffman.c's free_ppnode), as -2 - index */
static int ep(const huff_t *h, const node_t *p)
{
	const node_t *const *q = (const node_t *const *)p;

	if (!p)
		return -1;
	if (p >= h->nodeList && p < h->nodeList + 768)
		return (int)(p - h->nodeList);
	if (q >= (const node_t *const *)h->nodePtrs && q < (const node_t *const *)h->nodePtrs + 768)
		return -2 - (int)(q - (const node_t *const *)h->nodePtrs);
	fprintf(stderr, "huffgen: a nodePtrs entry points elsewhere\n");
	exit(1);
}

static int pi(const huff_t *h, node_t *const *p)
{
	return p ? (int)(p - h->nodePtrs) : -1;
}

static int same(const huff_t *a, const huff_t *b)
{
	int i;

	if (a->blocNode != b->blocNode || a->blocPtrs != b->blocPtrs || ni(a, a->tree) != ni(b, b->tree) ||
	    ni(a, a->lhead) != ni(b, b->lhead) || ni(a, a->ltail) != ni(b, b->ltail) ||
	    pi(a, a->freelist) != pi(b, b->freelist))
		return 0;
	for (i = 0; i <= HMAX; i++)
		if (ni(a, a->loc[i]) != ni(b, b->loc[i]))
			return 0;
	for (i = 0; i < 768; i++) {
		const node_t *x = &a->nodeList[i], *y = &b->nodeList[i];

		if (ni(a, x->left) != ni(b, y->left) || ni(a, x->right) != ni(b, y->right) ||
		    ni(a, x->parent) != ni(b, y->parent) || ni(a, x->next) != ni(b, y->next) ||
		    ni(a, x->prev) != ni(b, y->prev) || pi(a, x->head) != pi(b, y->head) ||
		    x->weight != y->weight || x->symbol != y->symbol || ep(a, a->nodePtrs[i]) != ep(b, b->nodePtrs[i]))
			return 0;
	}
	return 1;
}

int main(void)
{
	int i, j;

	Huff_Init(&built);
	for (i = 0; i < 256; i++)
		for (j = 0; j < msg_hData[i]; j++) {
			Huff_addRef(&built.compressor, (byte)i);
			Huff_addRef(&built.decompressor, (byte)i);
		}
	Huff_RestoreInit(&restored.compressor, &huffinit[0]);
	Huff_RestoreInit(&restored.decompressor, &huffinit[1]);
	if (!same(&built.compressor, &restored.compressor) || !same(&built.decompressor, &restored.decompressor)) {
		fprintf(stderr, "huffcheck: a restored tree differs\n");
		return 1;
	}
	printf("huffcheck: both restored trees equal the built ones (%d, %d nodes)\n",
	       restored.compressor.blocNode, restored.decompressor.blocNode);
	return 0;
}
