/* huffrestore.h -- the message Huffman tree from huffinit.h (AMIX).
 * MSG_initHuffman builds it from msg_hData with ~1.6 million adaptive steps,
 * 27 s on the 060; the table holds the finished tree as indices, and this
 * turns them back into pointers.  huffgen/ makes and checks the table. */
#include "huffinit.h"

static void Huff_RestoreInit( huff_t *h, const huffinit_t *t ) {
	int i;

#define HR_N( x ) ( ( x ) < 0 ? NULL : &h->nodeList[x] )
#define HR_P( x ) ( ( x ) < 0 ? NULL : &h->nodePtrs[x] )
	memset( h, 0, sizeof( *h ) );
	h->blocNode = t->bloc[0];
	h->blocPtrs = t->bloc[1];
	h->tree = HR_N( t->top[0] );
	h->lhead = HR_N( t->top[1] );
	h->ltail = HR_N( t->top[2] );
	h->freelist = HR_P( t->top[3] );
	for ( i = 0; i <= HMAX; i++ )
		h->loc[i] = HR_N( t->loc[i] );
	for ( i = 0; i < 768; i++ ) {
		node_t *n = &h->nodeList[i];
		const int *v = t->nodes[i];

		/* on the free list an entry points into nodePtrs itself */
		h->nodePtrs[i] = t->ptrs[i] <= -2 ? (node_t *)&h->nodePtrs[-2 - t->ptrs[i]] : HR_N( t->ptrs[i] );
		n->left = HR_N( v[0] );
		n->right = HR_N( v[1] );
		n->parent = HR_N( v[2] );
		n->next = HR_N( v[3] );
		n->prev = HR_N( v[4] );
		n->head = HR_P( v[5] );
		n->weight = v[6];
		n->symbol = v[7];
	}
#undef HR_N
#undef HR_P
}
