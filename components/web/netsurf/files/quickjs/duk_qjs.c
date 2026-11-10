/*
 * duk_qjs.c: the Duktape 2 C API calls NetSurf uses, on QuickJS-ng.
 * See duk_qjs.h for the model and the differences from Duktape.
 *
 * This file is MIT licensed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>

#include <quickjs.h>

#include "duk_qjs.h"

/* ------------------------------------------------------------------ */
/* data structures */

/* one hidden symbol: Duktape string bytes -> QuickJS symbol */
struct dukq_hidden {
	struct dukq_hidden *next;
	size_t len;
	JSValue sym;	/* the symbol (owned) */
	JSAtom atom;	/* its atom (owned) */
	char bytes[];	/* the Duktape bytes, NUL terminated */
};

#define DUKQ_HIDDEN_HASH 256

/* hidden property of an object that held a number, kept for its finalizer */
struct dukq_snap {
	struct dukq_snap *next;
	JSAtom key;	/* atom of a dukq_hidden, not owned */
	double val;
};

/* opaque of objects made by duk_push_object (NULL until needed) */
struct dukq_obj {
	duk_c_function fin;
	struct dukq_snap *snap;
};

/* finalizer waiting to run */
struct dukq_fin {
	struct dukq_fin *next;
	duk_c_function fin;
	struct dukq_snap *snap;
};

struct dukq_slot {
	JSValue v;
	const char *cs;		/* C string cache for duk_get_string etc. */
	size_t cslen;
	int cs_static;		/* cs belongs to the hidden table */
};

struct dukq_heap {
	JSRuntime *rt;
	void *udata;
	duk_memory_functions mf;
	duk_bool_t (*timeout_check)(void *udata);
	JSClassID obj_class;
	JSClassID thread_class;
	struct dukq_hidden *hidden[DUKQ_HIDDEN_HASH];
	struct dukq_fin *finq, **finq_tail;
	struct duk_hthread *threads;	/* all threads but the base */
	struct duk_hthread *base;
	int depth;			/* calls into JS in progress */
	int draining;
	int dying;
};

struct duk_hthread {
	struct dukq_heap *heap;
	struct duk_hthread *next;	/* in heap->threads */
	JSContext *jctx;
	JSValue objproto;		/* this context's Object.prototype */
	struct dukq_slot *st;
	int top, cap;
	int bottom;			/* current frame */
	JSValue this_val;		/* current frame's this (not owned) */
	jmp_buf *catcher;
	JSValue err;			/* error being thrown to catcher */
	struct dukq_snap *fin_snap;	/* finalizer frame: index 0 stand-in */
	int fin_frame;
	duk_c_function global_fin;	/* the adopted global object's finalizer */
	int dead;			/* thread object collected */
};

/* ------------------------------------------------------------------ */
/* helpers */

static DUK_QJS_NORETURN void dukq_fatal(const char *msg)
{
	fprintf(stderr, "duk_qjs: fatal: %s\n", msg);
	abort();
}

static unsigned dukq_hash(const char *s, size_t len)
{
	unsigned h = 5381;
	while (len--)
		h = h * 33 + (unsigned char)*s++;
	return h % DUKQ_HIDDEN_HASH;
}

static int dukq_is_hidden(const char *s, size_t len)
{
	unsigned char c;
	if (len == 0)
		return 0;
	c = (unsigned char)s[0];
	return c == 0xFF || c == 0x80 || c == 0x81 || c == 0x82;
}

static struct dukq_hidden *dukq_hidden_get(duk_context *ctx, const char *s, size_t len)
{
	struct dukq_heap *heap = ctx->heap;
	unsigned h = dukq_hash(s, len);
	struct dukq_hidden *e;
	for (e = heap->hidden[h]; e != NULL; e = e->next)
		if (e->len == len && memcmp(e->bytes, s, len) == 0)
			return e;
	e = malloc(sizeof(*e) + len + 1);
	if (e == NULL)
		dukq_fatal("out of memory");
	memcpy(e->bytes, s, len);
	e->bytes[len] = '\0';
	e->len = len;
	/* the description is for debugging only: skip the marker bytes */
	{
		size_t i = 0;
		while (i < len && ((unsigned char)s[i] >= 0x80))
			i++;
		e->sym = JS_NewSymbol(ctx->jctx, e->bytes + i, false);
	}
	if (JS_IsException(e->sym))
		dukq_fatal("cannot create symbol");
	e->atom = JS_ValueToAtom(ctx->jctx, e->sym);
	e->next = heap->hidden[h];
	heap->hidden[h] = e;
	return e;
}

/* hidden entry of a symbol value, or NULL */
static struct dukq_hidden *dukq_hidden_of(duk_context *ctx, JSValueConst v)
{
	int i;
	struct dukq_hidden *e;
	if (JS_VALUE_GET_TAG(v) != JS_TAG_SYMBOL)
		return NULL;
	for (i = 0; i < DUKQ_HIDDEN_HASH; i++)
		for (e = ctx->heap->hidden[i]; e != NULL; e = e->next)
			if (JS_VALUE_GET_PTR(e->sym) == JS_VALUE_GET_PTR(v))
				return e;
	return NULL;
}

static struct dukq_hidden *dukq_hidden_of_atom(struct dukq_heap *heap, JSAtom a)
{
	int i;
	struct dukq_hidden *e;
	for (i = 0; i < DUKQ_HIDDEN_HASH; i++)
		for (e = heap->hidden[i]; e != NULL; e = e->next)
			if (e->atom == a)
				return e;
	return NULL;
}

static void dukq_freeval(struct dukq_heap *heap, JSValue v)
{
	if (JS_VALUE_HAS_REF_COUNT(v))
		JS_FreeValueRT(heap->rt, v);
}

static void dukq_slot_clear_cs(struct dukq_heap *heap, struct dukq_slot *s)
{
	if (s->cs != NULL && !s->cs_static)
		JS_FreeCStringRT(heap->rt, s->cs);
	s->cs = NULL;
	s->cs_static = 0;
}

static void dukq_slot_free(struct dukq_heap *heap, struct dukq_slot *s)
{
	dukq_slot_clear_cs(heap, s);
	dukq_freeval(heap, s->v);
	s->v = JS_UNDEFINED;
}

static void dukq_grow(duk_context *ctx, int need)
{
	if (ctx->top + need <= ctx->cap)
		return;
	int ncap = ctx->cap ? ctx->cap * 2 : 64;
	while (ncap < ctx->top + need)
		ncap *= 2;
	struct dukq_slot *n = realloc(ctx->st, ncap * sizeof(*n));
	if (n == NULL)
		dukq_fatal("out of memory (value stack)");
	ctx->st = n;
	ctx->cap = ncap;
}

/* push, taking ownership of v */
static void dukq_push(duk_context *ctx, JSValue v)
{
	dukq_grow(ctx, 1);
	struct dukq_slot *s = &ctx->st[ctx->top++];
	s->v = v;
	s->cs = NULL;
	s->cslen = 0;
	s->cs_static = 0;
}

static void dukq_unwind(duk_context *ctx, int to)
{
	while (ctx->top > to)
		dukq_slot_free(ctx->heap, &ctx->st[--ctx->top]);
}

static DUK_QJS_NORETURN void dukq_throw(duk_context *ctx, JSValue err)
{
	if (ctx->catcher == NULL) {
		const char *s = JS_ToCString(ctx->jctx, err);
		fprintf(stderr, "duk_qjs: uncaught error outside any call: %s\n",
			s ? s : "?");
		dukq_fatal("uncaught error");
	}
	ctx->err = err;
	longjmp(*ctx->catcher, 1);
	/* AMIX's setjmp.h does not declare longjmp noreturn */
	__builtin_unreachable();
}

static DUK_QJS_NORETURN void dukq_throw_pending(duk_context *ctx)
{
	dukq_throw(ctx, JS_GetException(ctx->jctx));
}

static JSValue dukq_make_error(duk_context *ctx, int code, const char *msg)
{
	JSContext *j = ctx->jctx;
	switch (code) {
	case DUK_ERR_EVAL_ERROR: {
		JSValue g = JS_GetGlobalObject(j);
		JSValue c = JS_GetPropertyStr(j, g, "EvalError");
		JSValue m = JS_NewString(j, msg);
		JSValue e = JS_CallConstructor(j, c, 1, (JSValueConst *)&m);
		JS_FreeValue(j, m);
		JS_FreeValue(j, c);
		JS_FreeValue(j, g);
		if (JS_IsException(e))
			e = JS_GetException(j);
		return e;
	}
	case DUK_ERR_RANGE_ERROR: return JS_NewRangeError(j, "%s", msg);
	case DUK_ERR_REFERENCE_ERROR: return JS_NewReferenceError(j, "%s", msg);
	case DUK_ERR_SYNTAX_ERROR: return JS_NewSyntaxError(j, "%s", msg);
	case DUK_ERR_TYPE_ERROR: return JS_NewTypeError(j, "%s", msg);
	case DUK_ERR_URI_ERROR: {
		JSValue g = JS_GetGlobalObject(j);
		JSValue c = JS_GetPropertyStr(j, g, "URIError");
		JSValue m = JS_NewString(j, msg);
		JSValue e = JS_CallConstructor(j, c, 1, (JSValueConst *)&m);
		JS_FreeValue(j, m);
		JS_FreeValue(j, c);
		JS_FreeValue(j, g);
		if (JS_IsException(e))
			e = JS_GetException(j);
		return e;
	}
	default: return JS_NewPlainError(j, "%s", msg);
	}
}

static DUK_QJS_NORETURN void dukq_throw_code(duk_context *ctx, int code, const char *msg)
{
	dukq_throw(ctx, dukq_make_error(ctx, code, msg));
}

/* absolute index, or -1 */
static int dukq_abs(duk_context *ctx, duk_idx_t idx)
{
	int a;
	if (idx == DUK_INVALID_INDEX)
		return -1;
	a = idx >= 0 ? ctx->bottom + idx : ctx->top + idx;
	if (a < ctx->bottom || a >= ctx->top)
		return -1;
	return a;
}

static int dukq_req(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_abs(ctx, idx);
	if (a < 0) {
		char m[64];
		snprintf(m, sizeof(m), "invalid stack index %d", (int)idx);
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, m);
	}
	return a;
}

static JSValueConst dukq_val(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_abs(ctx, idx);
	return a < 0 ? JS_UNDEFINED : ctx->st[a].v;
}

/* atom for a C key (owned) */
static JSAtom dukq_atom_cstr(duk_context *ctx, const char *key, size_t len)
{
	if (dukq_is_hidden(key, len))
		return JS_DupAtom(ctx->jctx, dukq_hidden_get(ctx, key, len)->atom);
	JSAtom a = JS_NewAtomLen(ctx->jctx, key, len);
	if (a == JS_ATOM_NULL)
		dukq_throw_pending(ctx);
	return a;
}

/* atom for a key value (owned) */
static JSAtom dukq_atom_val(duk_context *ctx, JSValueConst v)
{
	JSAtom a = JS_ValueToAtom(ctx->jctx, v);
	if (a == JS_ATOM_NULL)
		dukq_throw_pending(ctx);
	return a;
}

static double dukq_number(JSValueConst v)
{
	int tag = JS_VALUE_GET_NORM_TAG(v);
	if (tag == JS_TAG_INT)
		return JS_VALUE_GET_INT(v);
	if (tag == JS_TAG_FLOAT64)
		return JS_VALUE_GET_FLOAT64(v);
	return NAN;
}

static int dukq_is_num(JSValueConst v)
{
	int tag = JS_VALUE_GET_NORM_TAG(v);
	return tag == JS_TAG_INT || tag == JS_TAG_FLOAT64;
}

/* the opaque of one of our objects, created on demand; NULL if not ours */
static struct dukq_obj *dukq_objdata(duk_context *ctx, JSValueConst v, int create)
{
	struct dukq_obj *o;
	if (JS_VALUE_GET_TAG(v) != JS_TAG_OBJECT ||
	    JS_GetClassID(v) != ctx->heap->obj_class)
		return NULL;
	o = JS_GetOpaque(v, ctx->heap->obj_class);
	if (o == NULL && create) {
		o = calloc(1, sizeof(*o));
		if (o == NULL)
			dukq_fatal("out of memory");
		JS_SetOpaque(v, o);
	}
	return o;
}

static void dukq_snap_free(struct dukq_snap *s)
{
	while (s != NULL) {
		struct dukq_snap *n = s->next;
		free(s);
		s = n;
	}
}

/* remember a hidden property that holds a number (NetSurf's private
 * pointers) so that the finalizer can read it after the object is gone */
static void dukq_snap_put(duk_context *ctx, JSValueConst obj, JSAtom key, JSValueConst val)
{
	struct dukq_obj *o;
	struct dukq_snap *s;
	if (!dukq_is_num(val))
		return;
	if (dukq_hidden_of_atom(ctx->heap, key) == NULL)
		return;
	o = dukq_objdata(ctx, obj, 1);
	if (o == NULL)
		return;
	for (s = o->snap; s != NULL; s = s->next)
		if (s->key == key) {
			s->val = dukq_number(val);
			return;
		}
	s = malloc(sizeof(*s));
	if (s == NULL)
		dukq_fatal("out of memory");
	s->key = key;
	s->val = dukq_number(val);
	s->next = o->snap;
	o->snap = s;
}

/* ------------------------------------------------------------------ */
/* classes and finalizers */

static void dukq_obj_finalizer(JSRuntime *rt, JSValueConst val)
{
	struct dukq_heap *heap = JS_GetRuntimeOpaque(rt);
	struct dukq_obj *o = JS_GetOpaque(val, heap->obj_class);
	if (o == NULL)
		return;
	if (o->fin != NULL) {
		struct dukq_fin *f = malloc(sizeof(*f));
		if (f == NULL)
			dukq_fatal("out of memory");
		f->fin = o->fin;
		f->snap = o->snap;
		f->next = NULL;
		*heap->finq_tail = f;
		heap->finq_tail = &f->next;
	} else {
		dukq_snap_free(o->snap);
	}
	free(o);
}

static void dukq_thread_finalizer(JSRuntime *rt, JSValueConst val)
{
	struct dukq_heap *heap = JS_GetRuntimeOpaque(rt);
	duk_context *t = JS_GetOpaque(val, heap->thread_class);
	if (t != NULL)
		t->dead = 1;
}

/* run one finalizer: index 0 is a stand-in for the dead object */
static void dukq_run_fin(duk_context *ctx, struct dukq_fin *f)
{
	int obottom = ctx->bottom;
	JSValue othis = ctx->this_val;
	jmp_buf *ocatch = ctx->catcher;
	struct dukq_snap *osnap = ctx->fin_snap;
	int ofin = ctx->fin_frame;
	jmp_buf jb;

	ctx->bottom = ctx->top;
	ctx->this_val = JS_UNDEFINED;
	ctx->fin_snap = f->snap;
	ctx->fin_frame = 1;
	dukq_push(ctx, JS_UNDEFINED);
	dukq_push(ctx, JS_FALSE);
	ctx->catcher = &jb;
	if (setjmp(jb) == 0) {
		f->fin(ctx);
	} else {
		dukq_freeval(ctx->heap, ctx->err);
		ctx->err = JS_UNDEFINED;
	}
	dukq_unwind(ctx, ctx->bottom);
	ctx->bottom = obottom;
	ctx->this_val = othis;
	ctx->catcher = ocatch;
	ctx->fin_snap = osnap;
	ctx->fin_frame = ofin;
}

static void dukq_free_thread(duk_context *t);

/* run waiting finalizers and free dead threads; only at safe points */
static void dukq_drain(struct dukq_heap *heap)
{
	if (heap->draining || heap->depth > 0)
		return;
	heap->draining = 1;
	for (;;) {
		int again = 0;
		while (heap->finq != NULL) {
			struct dukq_fin *f = heap->finq;
			heap->finq = f->next;
			if (heap->finq == NULL)
				heap->finq_tail = &heap->finq;
			dukq_run_fin(heap->base, f);
			dukq_snap_free(f->snap);
			free(f);
		}
		duk_context **pp = &heap->threads;
		while (*pp != NULL) {
			duk_context *t = *pp;
			if (t->dead) {
				*pp = t->next;
				dukq_free_thread(t);
				again = 1;
			} else {
				pp = &t->next;
			}
		}
		if (!again)
			break;
		if (heap->rt != NULL && !heap->dying)
			JS_RunGC(heap->rt);
	}
	heap->draining = 0;
}

static void dukq_run_jobs(struct dukq_heap *heap)
{
	JSContext *jc;
	int r;
	if (heap->depth > 0 || heap->dying)
		return;
	heap->depth++;
	while ((r = JS_ExecutePendingJob(heap->rt, &jc)) != 0) {
		if (r < 0) {
			JSValue e = JS_GetException(jc);
			const char *s = JS_ToCString(jc, e);
			fprintf(stderr, "duk_qjs: error in promise job: %s\n", s ? s : "?");
			if (s)
				JS_FreeCString(jc, s);
			JS_FreeValue(jc, e);
		}
	}
	heap->depth--;
}

static int dukq_interrupt(JSContext *jctx, void *opaque)
{
	struct dukq_heap *heap = opaque;
	(void)jctx;
	if (heap->timeout_check != NULL)
		return heap->timeout_check(heap->udata) ? 1 : 0;
	return 0;
}

static duk_context *dukq_new_thread(struct dukq_heap *heap)
{
	duk_context *t = calloc(1, sizeof(*t));
	JSValue o;
	if (t == NULL)
		return NULL;
	t->heap = heap;
	t->jctx = JS_NewContext(heap->rt);
	if (t->jctx == NULL) {
		free(t);
		return NULL;
	}
	JS_SetContextOpaque(t->jctx, t);
	o = JS_NewObject(t->jctx);
	t->objproto = JS_GetPrototype(t->jctx, o);
	JS_FreeValue(t->jctx, o);
	t->this_val = JS_UNDEFINED;
	t->err = JS_UNDEFINED;
	return t;
}

static void dukq_free_thread(duk_context *t)
{
	struct dukq_heap *heap = t->heap;
	if (t->global_fin != NULL && t->jctx != NULL) {
		/* the adopted Window: its finalizer sees the global object */
		int obottom = t->bottom;
		jmp_buf *ocatch = t->catcher;
		jmp_buf jb;
		t->bottom = t->top;
		dukq_push(t, JS_GetGlobalObject(t->jctx));
		dukq_push(t, JS_FALSE);
		t->catcher = &jb;
		heap->depth++;
		if (setjmp(jb) == 0) {
			t->global_fin(t);
		} else {
			dukq_freeval(heap, t->err);
			t->err = JS_UNDEFINED;
		}
		heap->depth--;
		dukq_unwind(t, t->bottom);
		t->bottom = obottom;
		t->catcher = ocatch;
		t->global_fin = NULL;
	}
	dukq_unwind(t, 0);
	free(t->st);
	t->st = NULL;
	if (t->jctx != NULL) {
		JS_FreeValue(t->jctx, t->objproto);
		JS_FreeContext(t->jctx);
		t->jctx = NULL;
	}
	free(t);
}

/* ------------------------------------------------------------------ */
/* heap and threads */

duk_context *duk_create_heap(duk_alloc_function alloc_func,
		duk_realloc_function realloc_func, duk_free_function free_func,
		void *heap_udata, duk_fatal_function fatal_handler)
{
	struct dukq_heap *heap = calloc(1, sizeof(*heap));
	JSClassDef od = { "Object", dukq_obj_finalizer, NULL, NULL, NULL };
	JSClassDef td = { "Thread", dukq_thread_finalizer, NULL, NULL, NULL };
	(void)fatal_handler;
	if (heap == NULL)
		return NULL;
	heap->udata = heap_udata;
	heap->mf.alloc_func = alloc_func;
	heap->mf.realloc_func = realloc_func;
	heap->mf.free_func = free_func;
	heap->mf.udata = heap_udata;
	heap->finq_tail = &heap->finq;
	heap->rt = JS_NewRuntime();
	if (heap->rt == NULL) {
		free(heap);
		return NULL;
	}
	JS_SetRuntimeOpaque(heap->rt, heap);
	JS_NewClassID(heap->rt, &heap->obj_class);
	JS_NewClass(heap->rt, heap->obj_class, &od);
	JS_NewClassID(heap->rt, &heap->thread_class);
	JS_NewClass(heap->rt, heap->thread_class, &td);
	JS_SetInterruptHandler(heap->rt, dukq_interrupt, heap);
	heap->base = dukq_new_thread(heap);
	if (heap->base == NULL) {
		JS_FreeRuntime(heap->rt);
		free(heap);
		return NULL;
	}
	return heap->base;
}

void duk_qjs_set_timeout_check(duk_context *ctx, duk_bool_t (*check)(void *udata))
{
	ctx->heap->timeout_check = check;
}

void duk_destroy_heap(duk_context *ctx)
{
	struct dukq_heap *heap;
	int i;
	if (ctx == NULL)
		return;
	heap = ctx->heap;
	dukq_drain(heap);
	/* threads still alive: their windows go now */
	while (heap->threads != NULL) {
		duk_context *t = heap->threads;
		heap->threads = t->next;
		dukq_free_thread(t);
	}
	JS_RunGC(heap->rt);
	dukq_drain(heap);
	/* the base context, then everything left */
	dukq_unwind(heap->base, 0);
	for (i = 0; i < DUKQ_HIDDEN_HASH; i++) {
		struct dukq_hidden *e = heap->hidden[i];
		while (e != NULL) {
			struct dukq_hidden *n = e->next;
			JS_FreeAtom(heap->base->jctx, e->atom);
			JS_FreeValue(heap->base->jctx, e->sym);
			free(e);
			e = n;
		}
		heap->hidden[i] = NULL;
	}
	JS_FreeValue(heap->base->jctx, heap->base->objproto);
	JS_FreeContext(heap->base->jctx);
	heap->base->jctx = NULL;
	JS_RunGC(heap->rt);
	heap->dying = 1;
	JS_FreeRuntime(heap->rt);
	heap->rt = NULL;
	/* finalizers queued while the runtime went: no JS left, they only
	 * read their snapshots */
	heap->depth = 0;
	dukq_drain(heap);
	free(heap->base->st);
	free(heap->base);
	free(heap);
}

void duk_get_memory_functions(duk_context *ctx, duk_memory_functions *out_funcs)
{
	*out_funcs = ctx->heap->mf;
}

void duk_gc(duk_context *ctx, duk_uint_t flags)
{
	(void)flags;
	JS_RunGC(ctx->heap->rt);
	dukq_drain(ctx->heap);
}

duk_idx_t duk_push_thread(duk_context *ctx)
{
	struct dukq_heap *heap = ctx->heap;
	duk_context *t = dukq_new_thread(heap);
	JSValue o;
	if (t == NULL)
		dukq_throw_code(ctx, DUK_ERR_ERROR, "cannot create thread");
	o = JS_NewObjectClass(ctx->jctx, heap->thread_class);
	if (JS_IsException(o)) {
		dukq_free_thread(t);
		dukq_throw_pending(ctx);
	}
	JS_SetOpaque(o, t);
	t->next = heap->threads;
	heap->threads = t;
	dukq_push(ctx, o);
	return ctx->top - 1 - ctx->bottom;
}

duk_context *duk_require_context(duk_context *ctx, duk_idx_t idx)
{
	JSValueConst v = ctx->st[dukq_req(ctx, idx)].v;
	duk_context *t = NULL;
	if (JS_VALUE_GET_TAG(v) == JS_TAG_OBJECT)
		t = JS_GetOpaque(v, ctx->heap->thread_class);
	if (t == NULL)
		dukq_throw_code(ctx, DUK_ERR_TYPE_ERROR, "not a thread");
	return t;
}

void duk_set_global_object(duk_context *ctx)
{
	JSContext *j = ctx->jctx;
	int a = dukq_req(ctx, -1);
	JSValueConst obj = ctx->st[a].v;
	JSValue g, proto;
	JSPropertyEnum *tab = NULL;
	uint32_t len = 0, i;
	struct dukq_obj *o;

	if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
		dukq_throw_code(ctx, DUK_ERR_TYPE_ERROR, "global must be an object");
	g = JS_GetGlobalObject(j);
	if (JS_GetOwnPropertyNames(j, &tab, &len, obj,
				   JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0) {
		JS_FreeValue(j, g);
		dukq_throw_pending(ctx);
	}
	for (i = 0; i < len; i++) {
		JSPropertyDescriptor d;
		int r = JS_GetOwnProperty(j, &d, obj, tab[i].atom);
		if (r <= 0)
			continue;
		JS_DefineProperty(j, g, tab[i].atom, d.value, d.getter, d.setter,
				  d.flags | JS_PROP_HAS_CONFIGURABLE |
				  JS_PROP_HAS_ENUMERABLE |
				  ((d.flags & JS_PROP_GETSET) ?
				   (JS_PROP_HAS_GET | JS_PROP_HAS_SET) :
				   (JS_PROP_HAS_WRITABLE | JS_PROP_HAS_VALUE)));
		JS_FreeValue(j, d.value);
		JS_FreeValue(j, d.getter);
		JS_FreeValue(j, d.setter);
	}
	JS_FreePropertyEnum(j, tab, len);
	proto = JS_GetPrototype(j, obj);
	if (JS_SetPrototype(j, g, proto) < 0) {
		JS_FreeValue(j, proto);
		JS_FreeValue(j, g);
		dukq_throw_pending(ctx);
	}
	JS_FreeValue(j, proto);
	JS_FreeValue(j, g);
	/* the global object now carries the window: its finalizer runs when
	 * the thread goes, not when the (now empty) object is collected */
	o = dukq_objdata(ctx, obj, 0);
	if (o != NULL) {
		ctx->global_fin = o->fin;
		o->fin = NULL;
	}
	duk_pop(ctx);
}

/* ------------------------------------------------------------------ */
/* stack */

duk_idx_t duk_get_top(duk_context *ctx)
{
	return ctx->top - ctx->bottom;
}

void duk_set_top(duk_context *ctx, duk_idx_t idx)
{
	int want = ctx->bottom + idx;
	if (idx < 0)
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, "invalid top");
	if (want < ctx->top) {
		dukq_unwind(ctx, want);
	} else {
		while (ctx->top < want)
			dukq_push(ctx, JS_UNDEFINED);
	}
}

duk_idx_t duk_normalize_index(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_abs(ctx, idx);
	return a < 0 ? DUK_INVALID_INDEX : a - ctx->bottom;
}

void duk_dup(duk_context *ctx, duk_idx_t from_idx)
{
	int a = dukq_req(ctx, from_idx);
	dukq_push(ctx, JS_DupValueRT(ctx->heap->rt, ctx->st[a].v));
}

void duk_dup_top(duk_context *ctx)
{
	duk_dup(ctx, -1);
}

void duk_insert(duk_context *ctx, duk_idx_t to_idx)
{
	int a = dukq_req(ctx, to_idx);
	int t = dukq_req(ctx, -1);
	struct dukq_slot s = ctx->st[t];
	memmove(&ctx->st[a + 1], &ctx->st[a], (t - a) * sizeof(s));
	ctx->st[a] = s;
}

void duk_remove(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	dukq_slot_free(ctx->heap, &ctx->st[a]);
	memmove(&ctx->st[a], &ctx->st[a + 1], (ctx->top - a - 1) * sizeof(ctx->st[0]));
	ctx->top--;
}

void duk_replace(duk_context *ctx, duk_idx_t to_idx)
{
	int a = dukq_req(ctx, to_idx);
	int t = dukq_req(ctx, -1);
	if (a == t) {
		return;
	}
	dukq_slot_free(ctx->heap, &ctx->st[a]);
	ctx->st[a] = ctx->st[t];
	ctx->top--;
}

void duk_pop_n(duk_context *ctx, duk_idx_t count)
{
	if (count < 0 || ctx->top - count < ctx->bottom)
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, "pop past frame");
	dukq_unwind(ctx, ctx->top - count);
}

void duk_pop(duk_context *ctx) { duk_pop_n(ctx, 1); }
void duk_pop_2(duk_context *ctx) { duk_pop_n(ctx, 2); }
void duk_pop_3(duk_context *ctx) { duk_pop_n(ctx, 3); }

/* ------------------------------------------------------------------ */
/* push */

void duk_push_undefined(duk_context *ctx) { dukq_push(ctx, JS_UNDEFINED); }
void duk_push_null(duk_context *ctx) { dukq_push(ctx, JS_NULL); }
void duk_push_boolean(duk_context *ctx, duk_bool_t val) { dukq_push(ctx, JS_NewBool(ctx->jctx, val != 0)); }
void duk_push_int(duk_context *ctx, duk_int_t val) { dukq_push(ctx, JS_NewInt32(ctx->jctx, val)); }
void duk_push_uint(duk_context *ctx, duk_uint_t val) { dukq_push(ctx, JS_NewUint32(ctx->jctx, val)); }
void duk_push_number(duk_context *ctx, duk_double_t val) { dukq_push(ctx, JS_NewNumber(ctx->jctx, val)); }

void duk_push_pointer(duk_context *ctx, void *p)
{
	dukq_push(ctx, JS_NewFloat64(ctx->jctx, (double)(uintptr_t)p));
}

const char *duk_push_lstring(duk_context *ctx, const char *str, duk_size_t len)
{
	struct dukq_slot *s;
	JSValue v;
	if (str == NULL) {
		str = "";
		len = 0;
	}
	if (dukq_is_hidden(str, len)) {
		struct dukq_hidden *e = dukq_hidden_get(ctx, str, len);
		dukq_push(ctx, JS_DupValue(ctx->jctx, e->sym));
		s = &ctx->st[ctx->top - 1];
		s->cs = e->bytes;
		s->cslen = e->len;
		s->cs_static = 1;
		return s->cs;
	}
	v = JS_NewStringLen(ctx->jctx, str, len);
	if (JS_IsException(v))
		dukq_throw_pending(ctx);
	dukq_push(ctx, v);
	return duk_get_string(ctx, -1);
}

const char *duk_push_string(duk_context *ctx, const char *str)
{
	if (str == NULL) {
		dukq_push(ctx, JS_NULL);
		return NULL;
	}
	return duk_push_lstring(ctx, str, strlen(str));
}

static const char *dukq_push_vsprintf(duk_context *ctx, const char *fmt, va_list ap)
{
	char buf[256];
	va_list aq;
	int n;
	va_copy(aq, ap);
	n = vsnprintf(buf, sizeof(buf), fmt, aq);
	va_end(aq);
	if (n < 0)
		n = 0;
	if ((size_t)n < sizeof(buf))
		return duk_push_lstring(ctx, buf, n);
	{
		char *b = malloc(n + 1);
		const char *r;
		if (b == NULL)
			dukq_fatal("out of memory");
		vsnprintf(b, n + 1, fmt, ap);
		JSValue v = JS_NewStringLen(ctx->jctx, b, n);
		free(b);
		if (JS_IsException(v))
			dukq_throw_pending(ctx);
		dukq_push(ctx, v);
		r = duk_get_string(ctx, -1);
		return r;
	}
}

const char *duk_push_sprintf(duk_context *ctx, const char *fmt, ...)
{
	va_list ap;
	const char *r;
	va_start(ap, fmt);
	r = dukq_push_vsprintf(ctx, fmt, ap);
	va_end(ap);
	return r;
}

duk_idx_t duk_push_object(duk_context *ctx)
{
	JSValue o = JS_NewObjectProtoClass(ctx->jctx, ctx->objproto, ctx->heap->obj_class);
	if (JS_IsException(o))
		dukq_throw_pending(ctx);
	dukq_push(ctx, o);
	return ctx->top - 1 - ctx->bottom;
}

duk_idx_t duk_push_array(duk_context *ctx)
{
	JSValue o = JS_NewArray(ctx->jctx);
	if (JS_IsException(o))
		dukq_throw_pending(ctx);
	dukq_push(ctx, o);
	return ctx->top - 1 - ctx->bottom;
}

void duk_push_this(duk_context *ctx)
{
	dukq_push(ctx, JS_DupValue(ctx->jctx, ctx->this_val));
}

void duk_push_global_object(duk_context *ctx)
{
	dukq_push(ctx, JS_GetGlobalObject(ctx->jctx));
}

/* JS -> C function call: a new frame holding the arguments */
static JSValue dukq_trampoline(JSContext *jctx, JSValueConst this_val, int argc,
			       JSValueConst *argv, int magic, void *opaque)
{
	duk_context *ctx = JS_GetContextOpaque(jctx);
	duk_c_function fn = (duk_c_function)opaque;
	/* QuickJS keeps the magic as uint16_t: DUK_VARARGS (-1) is 0xFFFF */
	int nargs = (int16_t)magic;
	int n, i;
	int obottom = ctx->bottom;
	JSValue othis = ctx->this_val;
	jmp_buf *ocatch = ctx->catcher;
	struct dukq_snap *osnap = ctx->fin_snap;
	int ofin = ctx->fin_frame;
	jmp_buf jb;
	volatile JSValue result = JS_UNDEFINED;

	ctx->bottom = ctx->top;
	ctx->this_val = this_val;
	ctx->fin_snap = NULL;
	ctx->fin_frame = 0;
	n = nargs == DUK_VARARGS ? argc : nargs;
	dukq_grow(ctx, n);
	for (i = 0; i < n; i++)
		dukq_push(ctx, i < argc ? JS_DupValue(jctx, argv[i]) : JS_UNDEFINED);
	ctx->catcher = &jb;
	ctx->heap->depth++;
	if (setjmp(jb) == 0) {
		duk_ret_t rc = fn(ctx);
		if (rc > 0) {
			if (ctx->top <= ctx->bottom)
				result = JS_Throw(jctx, JS_NewInternalError(jctx,
					"C function returned a value it did not push"));
			else
				result = JS_DupValue(jctx, ctx->st[ctx->top - 1].v);
		} else if (rc < 0) {
			char m[48];
			snprintf(m, sizeof(m), "error (rc %d)", (int)rc);
			result = JS_Throw(jctx, dukq_make_error(ctx, -rc, m));
		}
	} else {
		result = JS_Throw(jctx, ctx->err);
		ctx->err = JS_UNDEFINED;
	}
	ctx->heap->depth--;
	dukq_unwind(ctx, ctx->bottom);
	ctx->bottom = obottom;
	ctx->this_val = othis;
	ctx->catcher = ocatch;
	ctx->fin_snap = osnap;
	ctx->fin_frame = ofin;
	return result;
}

/* the C function of a function made by duk_push_c_function */
static struct dukq_hidden *dukq_cfn_key(duk_context *ctx)
{
	static const char k[] = "\x82" "duk_qjs_cfunction";
	return dukq_hidden_get(ctx, k, sizeof(k) - 1);
}

duk_idx_t duk_push_c_function(duk_context *ctx, duk_c_function func, duk_idx_t nargs)
{
	JSValue f = JS_NewCClosure(ctx->jctx, dukq_trampoline, "",
				   NULL, nargs == DUK_VARARGS ? 0 : nargs,
				   nargs, (void *)func);
	if (JS_IsException(f))
		dukq_throw_pending(ctx);
	/* constructible, as Duktape's C functions */
	JS_SetConstructorBit(ctx->jctx, f, true);
	JS_DefinePropertyValue(ctx->jctx, f, dukq_cfn_key(ctx)->atom,
			       JS_NewFloat64(ctx->jctx, (double)(uintptr_t)func), 0);
	dukq_push(ctx, f);
	return ctx->top - 1 - ctx->bottom;
}

static void *dukq_buf_realloc(JSRuntime *rt, void *opaque, void *ptr, size_t size)
{
	(void)rt;
	(void)opaque;
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	return realloc(ptr, size);
}

void *duk_push_buffer(duk_context *ctx, duk_size_t size, duk_bool_t dynamic)
{
	uint8_t *b = calloc(1, size ? size : 1);
	JSValue v;
	(void)dynamic;
	if (b == NULL)
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, "buffer too large");
	v = JS_NewArrayBuffer(ctx->jctx, b, size, 0, dukq_buf_realloc, NULL, false);
	if (JS_IsException(v)) {
		free(b);
		dukq_throw_pending(ctx);
	}
	dukq_push(ctx, v);
	return b;
}

void duk_push_buffer_object(duk_context *ctx, duk_idx_t idx_buffer,
		duk_size_t byte_offset, duk_size_t byte_length, duk_uint_t flags)
{
	JSValue args[3];
	JSValue v;
	JSTypedArrayEnum type;
	args[0] = ctx->st[dukq_req(ctx, idx_buffer)].v;
	args[1] = JS_NewNumber(ctx->jctx, (double)byte_offset);
	args[2] = JS_NewNumber(ctx->jctx, (double)byte_length);
	switch (flags) {
	case DUK_BUFOBJ_ARRAYBUFFER:
		dukq_push(ctx, JS_DupValue(ctx->jctx, args[0]));
		return;
	case DUK_BUFOBJ_UINT8ARRAY: type = JS_TYPED_ARRAY_UINT8; break;
	default: type = JS_TYPED_ARRAY_UINT8C; break;
	}
	v = JS_NewTypedArray(ctx->jctx, 3, args, type);
	if (JS_IsException(v))
		dukq_throw_pending(ctx);
	dukq_push(ctx, v);
}

static const char *dukq_vfmt(char *buf, size_t sz, const char *fmt, va_list ap)
{
	if (fmt == NULL)
		return "";
	vsnprintf(buf, sz, fmt, ap);
	return buf;
}

duk_idx_t duk_push_error_object(duk_context *ctx, duk_errcode_t err_code,
		const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	dukq_vfmt(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	dukq_push(ctx, dukq_make_error(ctx, err_code, fmt ? buf : ""));
	return ctx->top - 1 - ctx->bottom;
}

duk_ret_t duk_error(duk_context *ctx, duk_errcode_t err_code, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	dukq_vfmt(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	dukq_throw_code(ctx, err_code, fmt ? buf : "");
}

duk_ret_t duk_generic_error(duk_context *ctx, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	dukq_vfmt(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	dukq_throw_code(ctx, DUK_ERR_ERROR, fmt ? buf : "");
}

static const char *dukq_type_name(duk_context *ctx, JSValueConst v)
{
	switch (JS_VALUE_GET_NORM_TAG(v)) {
	case JS_TAG_UNDEFINED: return "undefined";
	case JS_TAG_NULL: return "null";
	case JS_TAG_BOOL: return "boolean";
	case JS_TAG_INT:
	case JS_TAG_FLOAT64: return "number";
	case JS_TAG_STRING:
	case JS_TAG_STRING_ROPE: return "string";
	case JS_TAG_SYMBOL: return dukq_hidden_of(ctx, v) ? "hidden" : "symbol";
	case JS_TAG_OBJECT: return JS_IsFunction(ctx->jctx, v) ? "function" : "object";
	case JS_TAG_FUNCTION_BYTECODE: return "compiled";
	default: return "other";
	}
}

void duk_push_context_dump(duk_context *ctx)
{
	char buf[1024];
	size_t o;
	int i;
	o = snprintf(buf, sizeof(buf), "ctx: top=%d, stack=[", ctx->top - ctx->bottom);
	for (i = ctx->bottom; i < ctx->top && o < sizeof(buf) - 32; i++)
		o += snprintf(buf + o, sizeof(buf) - o, "%s%s",
			      i == ctx->bottom ? "" : ",",
			      dukq_type_name(ctx, ctx->st[i].v));
	if (o < sizeof(buf) - 2) {
		buf[o++] = ']';
		buf[o] = '\0';
	}
	duk_push_string(ctx, buf);
}

/* ------------------------------------------------------------------ */
/* type checks and reads */

duk_int_t duk_get_type(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_abs(ctx, idx);
	if (a < 0)
		return DUK_TYPE_NONE;
	if (ctx->fin_frame && a == ctx->bottom)
		return DUK_TYPE_OBJECT;
	switch (JS_VALUE_GET_NORM_TAG(ctx->st[a].v)) {
	case JS_TAG_UNDEFINED: return DUK_TYPE_UNDEFINED;
	case JS_TAG_NULL: return DUK_TYPE_NULL;
	case JS_TAG_BOOL: return DUK_TYPE_BOOLEAN;
	case JS_TAG_INT:
	case JS_TAG_FLOAT64:
	case JS_TAG_BIG_INT:
	case JS_TAG_SHORT_BIG_INT: return DUK_TYPE_NUMBER;
	case JS_TAG_STRING:
	case JS_TAG_STRING_ROPE:
	case JS_TAG_SYMBOL: return DUK_TYPE_STRING;
	default: return DUK_TYPE_OBJECT;
	}
}

duk_bool_t duk_check_type(duk_context *ctx, duk_idx_t idx, duk_int_t type)
{
	return duk_get_type(ctx, idx) == type;
}

duk_bool_t duk_is_undefined(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_type(ctx, idx) == DUK_TYPE_UNDEFINED;
}

duk_bool_t duk_is_null(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_type(ctx, idx) == DUK_TYPE_NULL;
}

duk_bool_t duk_is_boolean(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_type(ctx, idx) == DUK_TYPE_BOOLEAN;
}

duk_bool_t duk_is_number(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_abs(ctx, idx);
	return a >= 0 && dukq_is_num(ctx->st[a].v);
}

duk_bool_t duk_is_string(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_type(ctx, idx) == DUK_TYPE_STRING;
}

duk_bool_t duk_get_boolean(duk_context *ctx, duk_idx_t idx)
{
	JSValueConst v = dukq_val(ctx, idx);
	return JS_VALUE_GET_TAG(v) == JS_TAG_BOOL && JS_VALUE_GET_BOOL(v);
}

static duk_int_t dukq_clamp_int(double d)
{
	if (isnan(d))
		return 0;
	if (d >= (double)INT_MAX)
		return INT_MAX;
	if (d <= (double)INT_MIN)
		return INT_MIN;
	return (duk_int_t)d;	/* truncates towards zero */
}

static duk_uint_t dukq_clamp_uint(double d)
{
	if (isnan(d) || d <= 0)
		return 0;
	if (d >= (double)UINT_MAX)
		return UINT_MAX;
	return (duk_uint_t)d;
}

duk_int_t duk_get_int(duk_context *ctx, duk_idx_t idx)
{
	JSValueConst v = dukq_val(ctx, idx);
	return dukq_is_num(v) ? dukq_clamp_int(dukq_number(v)) : 0;
}

duk_uint_t duk_get_uint(duk_context *ctx, duk_idx_t idx)
{
	JSValueConst v = dukq_val(ctx, idx);
	return dukq_is_num(v) ? dukq_clamp_uint(dukq_number(v)) : 0;
}

/* C string of a string (or hidden symbol) slot, cached in the slot */
static const char *dukq_slot_cstr(duk_context *ctx, int a, duk_size_t *out_len)
{
	struct dukq_slot *s = &ctx->st[a];
	if (s->cs == NULL) {
		int tag = JS_VALUE_GET_TAG(s->v);
		if (tag == JS_TAG_SYMBOL) {
			struct dukq_hidden *e = dukq_hidden_of(ctx, s->v);
			if (e == NULL) {
				if (out_len)
					*out_len = 0;
				return NULL;
			}
			s->cs = e->bytes;
			s->cslen = e->len;
			s->cs_static = 1;
		} else if (tag == JS_TAG_STRING || tag == JS_TAG_STRING_ROPE) {
			size_t len;
			const char *c = JS_ToCStringLen(ctx->jctx, &len, s->v);
			if (c == NULL)
				dukq_throw_pending(ctx);
			s->cs = c;
			s->cslen = len;
			s->cs_static = 0;
		} else {
			if (out_len)
				*out_len = 0;
			return NULL;
		}
	}
	if (out_len)
		*out_len = s->cslen;
	return s->cs;
}

const char *duk_get_string(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_abs(ctx, idx);
	if (a < 0)
		return NULL;
	return dukq_slot_cstr(ctx, a, NULL);
}

void *duk_get_pointer(duk_context *ctx, duk_idx_t idx)
{
	JSValueConst v = dukq_val(ctx, idx);
	if (!dukq_is_num(v))
		return NULL;
	return (void *)(uintptr_t)dukq_number(v);
}

duk_size_t duk_get_length(duk_context *ctx, duk_idx_t idx)
{
	JSValueConst v = dukq_val(ctx, idx);
	int tag = JS_VALUE_GET_TAG(v);
	int64_t len = 0;
	if (tag == JS_TAG_STRING || tag == JS_TAG_STRING_ROPE) {
		JSValue l = JS_GetPropertyStr(ctx->jctx, v, "length");
		len = dukq_is_num(l) ? (int64_t)dukq_number(l) : 0;
		JS_FreeValue(ctx->jctx, l);
	} else if (tag == JS_TAG_OBJECT) {
		if (JS_GetLength(ctx->jctx, v, &len) < 0)
			dukq_throw_pending(ctx);
	}
	return len < 0 ? 0 : (duk_size_t)len;
}

duk_bool_t duk_opt_boolean(duk_context *ctx, duk_idx_t idx, duk_bool_t def_value)
{
	int t = duk_get_type(ctx, idx);
	if (t == DUK_TYPE_NONE || t == DUK_TYPE_UNDEFINED)
		return def_value;
	return duk_require_boolean(ctx, idx);
}

static DUK_QJS_NORETURN void dukq_type_error(duk_context *ctx, duk_idx_t idx, const char *want)
{
	char m[96];
	snprintf(m, sizeof(m), "%s required, found %s (stack index %d)", want,
		 dukq_abs(ctx, idx) < 0 ? "none" : dukq_type_name(ctx, dukq_val(ctx, idx)),
		 (int)idx);
	dukq_throw_code(ctx, DUK_ERR_TYPE_ERROR, m);
}

duk_bool_t duk_require_boolean(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_boolean(ctx, idx))
		dukq_type_error(ctx, idx, "boolean");
	return duk_get_boolean(ctx, idx);
}

duk_double_t duk_require_number(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_number(ctx, idx))
		dukq_type_error(ctx, idx, "number");
	return dukq_number(dukq_val(ctx, idx));
}

duk_int_t duk_require_int(duk_context *ctx, duk_idx_t idx)
{
	return dukq_clamp_int(duk_require_number(ctx, idx));
}

const char *duk_require_lstring(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len)
{
	if (!duk_is_string(ctx, idx))
		dukq_type_error(ctx, idx, "string");
	return dukq_slot_cstr(ctx, dukq_abs(ctx, idx), out_len);
}

const char *duk_require_string(duk_context *ctx, duk_idx_t idx)
{
	return duk_require_lstring(ctx, idx, NULL);
}

duk_bool_t duk_strict_equals(duk_context *ctx, duk_idx_t idx1, duk_idx_t idx2)
{
	int a = dukq_abs(ctx, idx1), b = dukq_abs(ctx, idx2);
	if (a < 0 || b < 0)
		return 0;
	return JS_IsStrictEqual(ctx->jctx, ctx->st[a].v, ctx->st[b].v);
}

/* ------------------------------------------------------------------ */
/* coercion */

static void dukq_set_slot(duk_context *ctx, int a, JSValue v)
{
	dukq_slot_free(ctx->heap, &ctx->st[a]);
	ctx->st[a].v = v;
}

duk_bool_t duk_to_boolean(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	int b = JS_ToBool(ctx->jctx, ctx->st[a].v);
	if (b < 0)
		dukq_throw_pending(ctx);
	dukq_set_slot(ctx, a, JS_NewBool(ctx->jctx, b));
	return b;
}

static double dukq_to_number(duk_context *ctx, int a)
{
	double d;
	if (JS_ToFloat64(ctx->jctx, &d, ctx->st[a].v) < 0)
		dukq_throw_pending(ctx);
	return d;
}

duk_int_t duk_to_int(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	duk_int_t r = dukq_clamp_int(dukq_to_number(ctx, a));
	dukq_set_slot(ctx, a, JS_NewInt32(ctx->jctx, r));
	return r;
}

duk_uint_t duk_to_uint(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	duk_uint_t r = dukq_clamp_uint(dukq_to_number(ctx, a));
	dukq_set_slot(ctx, a, JS_NewUint32(ctx->jctx, r));
	return r;
}

duk_uint32_t duk_to_uint32(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	int32_t r;
	if (JS_ToInt32(ctx->jctx, &r, ctx->st[a].v) < 0)
		dukq_throw_pending(ctx);
	dukq_set_slot(ctx, a, JS_NewUint32(ctx->jctx, (uint32_t)r));
	return (uint32_t)r;
}

const char *duk_to_lstring(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len)
{
	int a = dukq_req(ctx, idx);
	int tag = JS_VALUE_GET_TAG(ctx->st[a].v);
	if (tag != JS_TAG_STRING && tag != JS_TAG_STRING_ROPE &&
	    !(tag == JS_TAG_SYMBOL && dukq_hidden_of(ctx, ctx->st[a].v))) {
		JSValue s = JS_ToString(ctx->jctx, ctx->st[a].v);
		if (JS_IsException(s))
			dukq_throw_pending(ctx);
		dukq_set_slot(ctx, a, s);
	}
	return dukq_slot_cstr(ctx, a, out_len);
}

const char *duk_to_string(duk_context *ctx, duk_idx_t idx)
{
	return duk_to_lstring(ctx, idx, NULL);
}

/* ToString that cannot fail; returns an owned string value */
static JSValue dukq_safe_string(duk_context *ctx, JSValueConst v)
{
	JSValue s;
	struct dukq_hidden *e = dukq_hidden_of(ctx, v);
	if (e != NULL)
		return JS_DupValue(ctx->jctx, e->sym);
	s = JS_ToString(ctx->jctx, v);
	if (JS_IsException(s)) {
		JSValue ex = JS_GetException(ctx->jctx);
		s = JS_ToString(ctx->jctx, ex);
		JS_FreeValue(ctx->jctx, ex);
		if (JS_IsException(s)) {
			ex = JS_GetException(ctx->jctx);
			JS_FreeValue(ctx->jctx, ex);
			s = JS_NewString(ctx->jctx, "Error");
		}
	}
	return s;
}

const char *duk_safe_to_lstring(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len)
{
	int a = dukq_req(ctx, idx);
	JSValue s = dukq_safe_string(ctx, ctx->st[a].v);
	dukq_set_slot(ctx, a, s);
	return dukq_slot_cstr(ctx, a, out_len);
}

const char *duk_safe_to_stacktrace(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	JSContext *j = ctx->jctx;
	JSValueConst v = ctx->st[a].v;
	JSValue r;
	if (JS_IsError(v)) {
		JSValue msg = dukq_safe_string(ctx, v);
		JSValue stack = JS_GetPropertyStr(j, v, "stack");
		if (JS_IsException(stack)) {
			JSValue ex = JS_GetException(j);
			JS_FreeValue(j, ex);
			stack = JS_UNDEFINED;
		}
		if (JS_IsString(stack)) {
			const char *m = JS_ToCString(j, msg);
			const char *st = JS_ToCString(j, stack);
			size_t ml = m ? strlen(m) : 0, sl = st ? strlen(st) : 0;
			char *b = malloc(ml + sl + 2);
			if (b != NULL) {
				memcpy(b, m ? m : "", ml);
				b[ml] = '\n';
				memcpy(b + ml + 1, st ? st : "", sl);
				b[ml + 1 + sl] = '\0';
				r = JS_NewString(j, b);
				free(b);
			} else {
				r = JS_DupValue(j, msg);
			}
			if (m)
				JS_FreeCString(j, m);
			if (st)
				JS_FreeCString(j, st);
			JS_FreeValue(j, msg);
		} else {
			r = msg;
		}
		JS_FreeValue(j, stack);
	} else {
		r = dukq_safe_string(ctx, v);
	}
	dukq_set_slot(ctx, a, r);
	return dukq_slot_cstr(ctx, a, NULL);
}

void duk_concat(duk_context *ctx, duk_idx_t count)
{
	int base, i;
	size_t total = 0, o = 0;
	char *b;
	if (count < 0 || ctx->top - count < ctx->bottom)
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, "concat past frame");
	base = ctx->top - count;
	for (i = 0; i < count; i++) {
		duk_size_t l;
		duk_to_lstring(ctx, base + i - ctx->bottom, &l);
		total += l;
	}
	b = malloc(total + 1);
	if (b == NULL)
		dukq_fatal("out of memory");
	for (i = 0; i < count; i++) {
		memcpy(b + o, ctx->st[base + i].cs, ctx->st[base + i].cslen);
		o += ctx->st[base + i].cslen;
	}
	b[o] = '\0';
	{
		JSValue v = JS_NewStringLen(ctx->jctx, b, o);
		free(b);
		if (JS_IsException(v))
			dukq_throw_pending(ctx);
		dukq_unwind(ctx, base);
		dukq_push(ctx, v);
	}
}

/* ------------------------------------------------------------------ */
/* properties */

/* finalizer frame: index 0 answers from the snapshot */
static int dukq_fin_get(duk_context *ctx, int a, JSAtom key)
{
	struct dukq_snap *s;
	if (!(ctx->fin_frame && a == ctx->bottom))
		return 0;
	for (s = ctx->fin_snap; s != NULL; s = s->next)
		if (s->key == key) {
			dukq_push(ctx, JS_NewFloat64(ctx->jctx, s->val));
			return 1;
		}
	dukq_push(ctx, JS_UNDEFINED);
	return 1;
}

static duk_bool_t dukq_get_atom(duk_context *ctx, int a, JSAtom key)
{
	JSValue v;
	if (dukq_fin_get(ctx, a, key)) {
		JS_FreeAtom(ctx->jctx, key);
		return !JS_IsUndefined(ctx->st[ctx->top - 1].v);
	}
	v = JS_GetProperty(ctx->jctx, ctx->st[a].v, key);
	JS_FreeAtom(ctx->jctx, key);
	if (JS_IsException(v))
		dukq_throw_pending(ctx);
	dukq_push(ctx, v);
	return !JS_IsUndefined(v);
}

duk_bool_t duk_get_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	int a = dukq_req(ctx, obj_idx);
	int k = dukq_req(ctx, -1);
	JSAtom key = dukq_atom_val(ctx, ctx->st[k].v);
	duk_bool_t r = dukq_get_atom(ctx, a, key);
	duk_remove(ctx, -2);	/* the key */
	return r;
}

duk_bool_t duk_get_prop_string(duk_context *ctx, duk_idx_t obj_idx, const char *key)
{
	int a = dukq_req(ctx, obj_idx);
	JSAtom k;
	/* finalizer stand-ins need no JS at all */
	if (ctx->fin_frame && a == ctx->bottom && dukq_is_hidden(key, strlen(key))) {
		struct dukq_hidden *e = dukq_hidden_get(ctx, key, strlen(key));
		struct dukq_snap *s;
		for (s = ctx->fin_snap; s != NULL; s = s->next)
			if (s->key == e->atom) {
				dukq_push(ctx, JS_NewFloat64(NULL, s->val));
				return 1;
			}
		dukq_push(ctx, JS_UNDEFINED);
		return 0;
	}
	k = dukq_atom_cstr(ctx, key, strlen(key));
	return dukq_get_atom(ctx, a, k);
}

duk_bool_t duk_get_prop_index(duk_context *ctx, duk_idx_t obj_idx, duk_uarridx_t arr_idx)
{
	int a = dukq_req(ctx, obj_idx);
	JSValue v = JS_GetPropertyUint32(ctx->jctx, ctx->st[a].v, arr_idx);
	if (JS_IsException(v))
		dukq_throw_pending(ctx);
	dukq_push(ctx, v);
	return !JS_IsUndefined(v);
}

static duk_bool_t dukq_put_atom(duk_context *ctx, int a, JSAtom key, int vidx)
{
	JSValueConst obj = ctx->st[a].v;
	JSValue val = JS_DupValue(ctx->jctx, ctx->st[vidx].v);
	int r;
	dukq_snap_put(ctx, obj, key, val);
	r = JS_SetProperty(ctx->jctx, obj, key, val);
	JS_FreeAtom(ctx->jctx, key);
	if (r < 0)
		dukq_throw_pending(ctx);
	return r > 0;
}

duk_bool_t duk_put_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	int a = dukq_req(ctx, obj_idx);
	int k = dukq_req(ctx, -2);
	int v = dukq_req(ctx, -1);
	JSAtom key = dukq_atom_val(ctx, ctx->st[k].v);
	duk_bool_t r = dukq_put_atom(ctx, a, key, v);
	dukq_unwind(ctx, ctx->top - 2);
	return r;
}

duk_bool_t duk_put_prop_string(duk_context *ctx, duk_idx_t obj_idx, const char *key)
{
	int a = dukq_req(ctx, obj_idx);
	int v = dukq_req(ctx, -1);
	JSAtom k = dukq_atom_cstr(ctx, key, strlen(key));
	duk_bool_t r = dukq_put_atom(ctx, a, k, v);
	dukq_unwind(ctx, ctx->top - 1);
	return r;
}

duk_bool_t duk_put_prop_index(duk_context *ctx, duk_idx_t obj_idx, duk_uarridx_t arr_idx)
{
	int a = dukq_req(ctx, obj_idx);
	int v = dukq_req(ctx, -1);
	int r = JS_SetPropertyUint32(ctx->jctx, ctx->st[a].v, arr_idx,
				     JS_DupValue(ctx->jctx, ctx->st[v].v));
	if (r < 0)
		dukq_throw_pending(ctx);
	dukq_unwind(ctx, ctx->top - 1);
	return r > 0;
}

static duk_bool_t dukq_del_atom(duk_context *ctx, int a, JSAtom key)
{
	/* no throw: Duktape's eval code made deletable globals (NetSurf
	 * deletes the "NetSurf" var of generics.js), QuickJS's are not */
	int r = JS_DeleteProperty(ctx->jctx, ctx->st[a].v, key, 0);
	JS_FreeAtom(ctx->jctx, key);
	if (r < 0)
		dukq_throw_pending(ctx);
	return r > 0;
}

duk_bool_t duk_del_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	int a = dukq_req(ctx, obj_idx);
	int k = dukq_req(ctx, -1);
	JSAtom key = dukq_atom_val(ctx, ctx->st[k].v);
	duk_bool_t r = dukq_del_atom(ctx, a, key);
	dukq_unwind(ctx, ctx->top - 1);
	return r;
}

duk_bool_t duk_del_prop_string(duk_context *ctx, duk_idx_t obj_idx, const char *key)
{
	int a = dukq_req(ctx, obj_idx);
	return dukq_del_atom(ctx, a, dukq_atom_cstr(ctx, key, strlen(key)));
}

duk_bool_t duk_del_prop_index(duk_context *ctx, duk_idx_t obj_idx, duk_uarridx_t arr_idx)
{
	int a = dukq_req(ctx, obj_idx);
	JSAtom key = JS_NewAtomUInt32(ctx->jctx, arr_idx);
	return dukq_del_atom(ctx, a, key);
}

duk_bool_t duk_has_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	int a = dukq_req(ctx, obj_idx);
	int k = dukq_req(ctx, -1);
	JSAtom key;
	int r;
	if (JS_VALUE_GET_TAG(ctx->st[a].v) != JS_TAG_OBJECT)
		dukq_throw_code(ctx, DUK_ERR_TYPE_ERROR, "has_prop on a non-object");
	key = dukq_atom_val(ctx, ctx->st[k].v);
	r = JS_HasProperty(ctx->jctx, ctx->st[a].v, key);
	JS_FreeAtom(ctx->jctx, key);
	if (r < 0)
		dukq_throw_pending(ctx);
	dukq_unwind(ctx, ctx->top - 1);
	return r > 0;
}

void duk_def_prop(duk_context *ctx, duk_idx_t obj_idx, duk_uint_t flags)
{
	int a = dukq_req(ctx, obj_idx);
	int nvals = ((flags & DUK_DEFPROP_HAVE_VALUE) ? 1 : 0) +
		((flags & DUK_DEFPROP_HAVE_GETTER) ? 1 : 0) +
		((flags & DUK_DEFPROP_HAVE_SETTER) ? 1 : 0);
	int k = dukq_req(ctx, -1 - nvals);
	int i = k + 1;
	JSValueConst val = JS_UNDEFINED, get = JS_UNDEFINED, set = JS_UNDEFINED;
	int jf = 0;
	JSAtom key;
	int r;

	if (flags & DUK_DEFPROP_HAVE_VALUE) {
		val = ctx->st[i++].v;
		jf |= JS_PROP_HAS_VALUE;
	}
	if (flags & DUK_DEFPROP_HAVE_GETTER) {
		get = ctx->st[i++].v;
		jf |= JS_PROP_HAS_GET;
	}
	if (flags & DUK_DEFPROP_HAVE_SETTER) {
		set = ctx->st[i++].v;
		jf |= JS_PROP_HAS_SET;
	}
	if (flags & DUK_DEFPROP_HAVE_WRITABLE) {
		jf |= JS_PROP_HAS_WRITABLE;
		if (flags & DUK_DEFPROP_WRITABLE)
			jf |= JS_PROP_WRITABLE;
	}
	if (flags & DUK_DEFPROP_HAVE_ENUMERABLE) {
		jf |= JS_PROP_HAS_ENUMERABLE;
		if (flags & DUK_DEFPROP_ENUMERABLE)
			jf |= JS_PROP_ENUMERABLE;
	}
	if (flags & DUK_DEFPROP_HAVE_CONFIGURABLE) {
		jf |= JS_PROP_HAS_CONFIGURABLE;
		if (flags & DUK_DEFPROP_CONFIGURABLE)
			jf |= JS_PROP_CONFIGURABLE;
	}
	jf |= JS_PROP_THROW;
	key = dukq_atom_val(ctx, ctx->st[k].v);
	if (flags & DUK_DEFPROP_HAVE_VALUE)
		dukq_snap_put(ctx, ctx->st[a].v, key, val);
	r = JS_DefineProperty(ctx->jctx, ctx->st[a].v, key, val, get, set, jf);
	JS_FreeAtom(ctx->jctx, key);
	if (r < 0)
		dukq_throw_pending(ctx);
	dukq_unwind(ctx, k);
}

duk_bool_t duk_get_global_string(duk_context *ctx, const char *key)
{
	duk_bool_t r;
	duk_push_global_object(ctx);
	r = duk_get_prop_string(ctx, -1, key);
	duk_remove(ctx, -2);
	return r;
}

duk_bool_t duk_put_global_string(duk_context *ctx, const char *key)
{
	duk_bool_t r;
	duk_push_global_object(ctx);
	duk_insert(ctx, -2);
	r = duk_put_prop_string(ctx, -2, key);
	duk_pop(ctx);
	return r;
}

void duk_get_prototype(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	JSValue p;
	if (JS_VALUE_GET_TAG(ctx->st[a].v) != JS_TAG_OBJECT) {
		dukq_push(ctx, JS_UNDEFINED);
		return;
	}
	p = JS_GetPrototype(ctx->jctx, ctx->st[a].v);
	if (JS_IsException(p))
		dukq_throw_pending(ctx);
	if (JS_IsNull(p))
		p = JS_UNDEFINED;
	dukq_push(ctx, p);
}

void duk_set_prototype(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	int p = dukq_req(ctx, -1);
	JSValueConst proto = ctx->st[p].v;
	struct dukq_obj *po;
	if (JS_IsUndefined(proto))
		proto = JS_NULL;
	if (JS_SetPrototype(ctx->jctx, ctx->st[a].v, proto) < 0)
		dukq_throw_pending(ctx);
	/* Duktape looks finalizers up through the prototype chain when the
	 * object dies; QuickJS has dropped the chain by then: take it now */
	po = dukq_objdata(ctx, proto, 0);
	if (po != NULL && po->fin != NULL) {
		struct dukq_obj *o = dukq_objdata(ctx, ctx->st[a].v, 1);
		if (o != NULL && o->fin == NULL)
			o->fin = po->fin;
	}
	duk_pop(ctx);
}

void duk_set_finalizer(duk_context *ctx, duk_idx_t idx)
{
	int a = dukq_req(ctx, idx);
	int f = dukq_req(ctx, -1);
	JSValue c = JS_GetProperty(ctx->jctx, ctx->st[f].v, dukq_cfn_key(ctx)->atom);
	struct dukq_obj *o;
	if (JS_IsException(c))
		dukq_throw_pending(ctx);
	if (!dukq_is_num(c)) {
		JS_FreeValue(ctx->jctx, c);
		dukq_throw_code(ctx, DUK_ERR_TYPE_ERROR,
				"finalizers must be C functions");
	}
	o = dukq_objdata(ctx, ctx->st[a].v, 1);
	if (o == NULL)
		dukq_throw_code(ctx, DUK_ERR_TYPE_ERROR,
				"finalizer on an object not made by duk_push_object");
	o->fin = (duk_c_function)(uintptr_t)dukq_number(c);
	duk_pop(ctx);
}

/* ------------------------------------------------------------------ */
/* calls and compiling */

/* call: [... func (this) args] -> [... result]; returns 0, or 1 with the
 * error in place of the result (protect), or throws */
static duk_int_t dukq_call(duk_context *ctx, duk_idx_t nargs, int has_this, int protect)
{
	JSContext *j = ctx->jctx;
	int fidx, i;
	JSValue func, thisv, res;
	JSValue argbuf[16], *argv = argbuf;

	if (nargs < 0 || ctx->top - nargs - 1 - has_this < ctx->bottom)
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, "invalid call arguments");
	fidx = ctx->top - nargs - 1 - has_this;
	func = ctx->st[fidx].v;
	thisv = has_this ? ctx->st[fidx + 1].v : JS_UNDEFINED;
	if (nargs > 16) {
		argv = malloc(nargs * sizeof(JSValue));
		if (argv == NULL)
			dukq_fatal("out of memory");
	}
	for (i = 0; i < nargs; i++)
		argv[i] = ctx->st[fidx + 1 + has_this + i].v;

	ctx->heap->depth++;
	if (JS_VALUE_GET_TAG(func) == JS_TAG_FUNCTION_BYTECODE)
		res = JS_EvalFunction(j, JS_DupValue(j, func));
	else
		res = JS_Call(j, func, thisv, nargs, (JSValueConst *)argv);
	ctx->heap->depth--;
	if (argv != argbuf)
		free(argv);
	if (JS_IsException(res)) {
		JSValue e = JS_GetException(j);
		dukq_unwind(ctx, fidx);
		if (!protect)
			dukq_throw(ctx, e);
		dukq_push(ctx, e);
		dukq_run_jobs(ctx->heap);
		dukq_drain(ctx->heap);
		return DUK_EXEC_ERROR;
	}
	dukq_unwind(ctx, fidx);
	dukq_push(ctx, res);
	dukq_run_jobs(ctx->heap);
	dukq_drain(ctx->heap);
	return DUK_EXEC_SUCCESS;
}

void duk_call(duk_context *ctx, duk_idx_t nargs)
{
	dukq_call(ctx, nargs, 0, 0);
}

duk_int_t duk_pcall(duk_context *ctx, duk_idx_t nargs)
{
	return dukq_call(ctx, nargs, 0, 1);
}

duk_int_t duk_pcall_method(duk_context *ctx, duk_idx_t nargs)
{
	return dukq_call(ctx, nargs, 1, 1);
}

duk_int_t duk_safe_call(duk_context *ctx, duk_safe_call_function func,
		void *udata, duk_idx_t nargs, duk_idx_t nrets)
{
	int obottom = ctx->bottom;
	jmp_buf *ocatch = ctx->catcher;
	jmp_buf jb;
	int base;
	volatile duk_int_t status;
	volatile int rc = 0;

	if (nargs < 0 || nrets < 0 || ctx->top - nargs < ctx->bottom)
		dukq_throw_code(ctx, DUK_ERR_RANGE_ERROR, "invalid safe call");
	base = ctx->top - nargs;
	ctx->bottom = base;
	ctx->catcher = &jb;
	if (setjmp(jb) == 0) {
		rc = func(ctx, udata);
		if (rc < 0) {
			char m[48];
			snprintf(m, sizeof(m), "error (rc %d)", rc);
			ctx->err = dukq_make_error(ctx, -rc, m);
			goto failed;
		}
		if (rc > ctx->top - base)
			rc = ctx->top - base;
		{
			/* keep the top rc values, as nrets values at base */
			int from = ctx->top - rc, i;
			int keep = rc < nrets ? rc : nrets;
			struct dukq_slot tmp[8], *t = tmp;
			if (keep > 8) {
				t = malloc(keep * sizeof(*t));
				if (t == NULL)
					dukq_fatal("out of memory");
			}
			for (i = 0; i < keep; i++) {
				t[i] = ctx->st[from + i];
				ctx->st[from + i].v = JS_UNDEFINED;
				ctx->st[from + i].cs = NULL;
				ctx->st[from + i].cs_static = 0;
			}
			dukq_unwind(ctx, base);
			for (i = 0; i < keep; i++) {
				dukq_grow(ctx, 1);
				ctx->st[ctx->top++] = t[i];
			}
			if (t != tmp)
				free(t);
			for (; i < nrets; i++)
				dukq_push(ctx, JS_UNDEFINED);
		}
		status = DUK_EXEC_SUCCESS;
	} else {
failed:
		dukq_unwind(ctx, base);
		dukq_push(ctx, ctx->err);
		ctx->err = JS_UNDEFINED;
		{
			int i;
			for (i = 1; i < nrets; i++)
				dukq_push(ctx, JS_UNDEFINED);
		}
		status = DUK_EXEC_ERROR;
	}
	ctx->bottom = obottom;
	ctx->catcher = ocatch;
	return status;
}

static duk_int_t dukq_compile(duk_context *ctx, duk_uint_t flags, const char *src,
			      size_t len, const char *filename, int pop)
{
	JSContext *j = ctx->jctx;
	char *buf;
	JSValue r;
	if (flags & DUK_COMPILE_FUNCTION) {
		/* "function (...) {...}": evaluate it as an expression */
		buf = malloc(len + 3);
		if (buf == NULL)
			dukq_fatal("out of memory");
		buf[0] = '(';
		memcpy(buf + 1, src, len);
		buf[len + 1] = ')';
		buf[len + 2] = '\0';
		ctx->heap->depth++;
		r = JS_Eval(j, buf, len + 2, filename, JS_EVAL_TYPE_GLOBAL |
			    ((flags & DUK_COMPILE_STRICT) ? JS_EVAL_FLAG_STRICT : 0));
		ctx->heap->depth--;
	} else {
		buf = malloc(len + 1);
		if (buf == NULL)
			dukq_fatal("out of memory");
		memcpy(buf, src, len);
		buf[len] = '\0';
		r = JS_Eval(j, buf, len, filename, JS_EVAL_TYPE_GLOBAL |
			    JS_EVAL_FLAG_COMPILE_ONLY |
			    ((flags & DUK_COMPILE_STRICT) ? JS_EVAL_FLAG_STRICT : 0));
	}
	free(buf);
	dukq_unwind(ctx, ctx->top - pop);
	if (JS_IsException(r)) {
		dukq_push(ctx, JS_GetException(j));
		return 1;
	}
	dukq_push(ctx, r);
	return 0;
}

duk_int_t duk_pcompile(duk_context *ctx, duk_uint_t flags)
{
	/* [... source filename] -> [... function] */
	duk_size_t len;
	const char *src, *fn;
	char *fncopy, *srccopy;
	duk_int_t r;
	if (duk_get_top(ctx) < 2)
		return 1;
	fn = duk_safe_to_string(ctx, -1);
	fncopy = strdup(fn ? fn : "?");
	src = duk_safe_to_lstring(ctx, -2, &len);
	srccopy = malloc(len + 1);
	if (fncopy == NULL || srccopy == NULL)
		dukq_fatal("out of memory");
	memcpy(srccopy, src, len);
	srccopy[len] = '\0';
	r = dukq_compile(ctx, flags, srccopy, len, fncopy, 2);
	free(srccopy);
	free(fncopy);
	return r;
}

duk_int_t duk_pcompile_lstring_filename(duk_context *ctx, duk_uint_t flags,
		const char *src, duk_size_t len)
{
	/* [... filename] -> [... function] */
	const char *fn;
	char *fncopy;
	duk_int_t r;
	if (duk_get_top(ctx) < 1)
		return 1;
	fn = duk_safe_to_string(ctx, -1);
	fncopy = strdup(fn ? fn : "?");
	if (fncopy == NULL)
		dukq_fatal("out of memory");
	r = dukq_compile(ctx, flags, src, len, fncopy, 1);
	free(fncopy);
	return r;
}
