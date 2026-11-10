/*
 * duk_qjs.h: the part of the Duktape 2 C API that NetSurf uses, on
 * QuickJS-ng.
 *
 * NetSurf's JavaScript layer (dukky.c and the nsgenbind-generated bindings)
 * is written against Duktape's value-stack API.  This header and duk_qjs.c
 * give the same calls on top of QuickJS-ng, so NetSurf's bindings stay
 * unchanged and pages get a modern (ES2023+) engine.
 *
 * Differences from Duktape that matter to callers:
 *  - Hidden symbols (strings starting with 0xFF, 0x80, 0x81 or 0x82) are
 *    QuickJS symbols, one per distinct byte string and heap.
 *  - Pointers are numbers (duk_get_pointer accepts any number).
 *  - duk_push_thread makes a new QuickJS context: its own global object and
 *    built-ins.  duk_set_global_object moves the given object's properties and
 *    prototype onto that context's global object (QuickJS cannot replace it).
 *  - Finalizers (duk_set_finalizer, inherited at duk_set_prototype time) run
 *    after the object is gone: the finalizer's index 0 is a stand-in that
 *    answers duk_get_prop_string for the hidden properties that held numbers
 *    or pointers, nothing else.
 *
 * This file is MIT licensed, as Duktape's API it mirrors.
 */

#ifndef DUK_QJS_H
#define DUK_QJS_H

/* Duktape's own header brings these in, and the bindings rely on it */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>

#define DUK_VERSION 20700L
#define DUK_QJS 1

typedef int duk_int_t;
typedef unsigned int duk_uint_t;
typedef int32_t duk_int32_t;
typedef uint32_t duk_uint32_t;
typedef int duk_small_int_t;
typedef int duk_idx_t;
typedef int duk_ret_t;
typedef int duk_bool_t;
typedef int duk_errcode_t;
typedef size_t duk_size_t;
typedef double duk_double_t;
typedef uint32_t duk_uarridx_t;
typedef int duk_codepoint_t;

typedef struct duk_hthread duk_context;

typedef duk_ret_t (*duk_c_function)(duk_context *ctx);
typedef void *(*duk_alloc_function)(void *udata, duk_size_t size);
typedef void *(*duk_realloc_function)(void *udata, void *ptr, duk_size_t size);
typedef void (*duk_free_function)(void *udata, void *ptr);
typedef void (*duk_fatal_function)(void *udata, const char *msg);
typedef duk_ret_t (*duk_safe_call_function)(duk_context *ctx, void *udata);

typedef struct duk_memory_functions {
	duk_alloc_function alloc_func;
	duk_realloc_function realloc_func;
	duk_free_function free_func;
	void *udata;
} duk_memory_functions;

#define DUK_INVALID_INDEX	INT_MIN
#define DUK_VARARGS		((duk_int_t) -1)

#define DUK_TYPE_NONE		0
#define DUK_TYPE_UNDEFINED	1
#define DUK_TYPE_NULL		2
#define DUK_TYPE_BOOLEAN	3
#define DUK_TYPE_NUMBER		4
#define DUK_TYPE_STRING		5
#define DUK_TYPE_OBJECT		6
#define DUK_TYPE_BUFFER		7
#define DUK_TYPE_POINTER	8
#define DUK_TYPE_LIGHTFUNC	9

#define DUK_ERR_NONE		0
#define DUK_ERR_ERROR		1
#define DUK_ERR_EVAL_ERROR	2
#define DUK_ERR_RANGE_ERROR	3
#define DUK_ERR_REFERENCE_ERROR	4
#define DUK_ERR_SYNTAX_ERROR	5
#define DUK_ERR_TYPE_ERROR	6
#define DUK_ERR_URI_ERROR	7

#define DUK_RET_ERROR		(-DUK_ERR_ERROR)
#define DUK_RET_EVAL_ERROR	(-DUK_ERR_EVAL_ERROR)
#define DUK_RET_RANGE_ERROR	(-DUK_ERR_RANGE_ERROR)
#define DUK_RET_REFERENCE_ERROR	(-DUK_ERR_REFERENCE_ERROR)
#define DUK_RET_SYNTAX_ERROR	(-DUK_ERR_SYNTAX_ERROR)
#define DUK_RET_TYPE_ERROR	(-DUK_ERR_TYPE_ERROR)
#define DUK_RET_URI_ERROR	(-DUK_ERR_URI_ERROR)

#define DUK_EXEC_SUCCESS	0
#define DUK_EXEC_ERROR		1

#define DUK_COMPILE_EVAL	(1U << 3)
#define DUK_COMPILE_FUNCTION	(1U << 4)
#define DUK_COMPILE_STRICT	(1U << 5)

#define DUK_DEFPROP_WRITABLE		(1U << 0)
#define DUK_DEFPROP_ENUMERABLE		(1U << 1)
#define DUK_DEFPROP_CONFIGURABLE	(1U << 2)
#define DUK_DEFPROP_HAVE_WRITABLE	(1U << 3)
#define DUK_DEFPROP_HAVE_ENUMERABLE	(1U << 4)
#define DUK_DEFPROP_HAVE_CONFIGURABLE	(1U << 5)
#define DUK_DEFPROP_HAVE_VALUE		(1U << 6)
#define DUK_DEFPROP_HAVE_GETTER		(1U << 7)
#define DUK_DEFPROP_HAVE_SETTER		(1U << 8)
#define DUK_DEFPROP_FORCE		(1U << 9)

#define DUK_GC_COMPACT		(1U << 0)

#define DUK_BUFOBJ_ARRAYBUFFER		0
#define DUK_BUFOBJ_UINT8ARRAY		3
#define DUK_BUFOBJ_UINT8CLAMPEDARRAY	4

#if defined(__GNUC__)
#define DUK_QJS_NORETURN __attribute__((noreturn))
#define DUK_QJS_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define DUK_QJS_NORETURN
#define DUK_QJS_PRINTF(f, a)
#endif

/* heap and threads */
duk_context *duk_create_heap(duk_alloc_function alloc_func,
		duk_realloc_function realloc_func, duk_free_function free_func,
		void *heap_udata, duk_fatal_function fatal_handler);
void duk_destroy_heap(duk_context *ctx);
void duk_get_memory_functions(duk_context *ctx, duk_memory_functions *out_funcs);
void duk_gc(duk_context *ctx, duk_uint_t flags);
duk_idx_t duk_push_thread(duk_context *ctx);
duk_context *duk_require_context(duk_context *ctx, duk_idx_t idx);
void duk_set_global_object(duk_context *ctx);
/* not Duktape: execution time check, called now and then while JS runs;
 * returning true stops the running script with an uncatchable error */
void duk_qjs_set_timeout_check(duk_context *ctx, duk_bool_t (*check)(void *udata));

/* stack */
duk_idx_t duk_get_top(duk_context *ctx);
void duk_set_top(duk_context *ctx, duk_idx_t idx);
duk_idx_t duk_normalize_index(duk_context *ctx, duk_idx_t idx);
void duk_dup(duk_context *ctx, duk_idx_t from_idx);
void duk_dup_top(duk_context *ctx);
void duk_insert(duk_context *ctx, duk_idx_t to_idx);
void duk_remove(duk_context *ctx, duk_idx_t idx);
void duk_replace(duk_context *ctx, duk_idx_t to_idx);
void duk_pop(duk_context *ctx);
void duk_pop_2(duk_context *ctx);
void duk_pop_3(duk_context *ctx);
void duk_pop_n(duk_context *ctx, duk_idx_t count);

/* push */
void duk_push_undefined(duk_context *ctx);
void duk_push_null(duk_context *ctx);
void duk_push_boolean(duk_context *ctx, duk_bool_t val);
#define duk_push_bool(ctx, val) duk_push_boolean((ctx), (val))
void duk_push_int(duk_context *ctx, duk_int_t val);
void duk_push_uint(duk_context *ctx, duk_uint_t val);
void duk_push_number(duk_context *ctx, duk_double_t val);
const char *duk_push_string(duk_context *ctx, const char *str);
const char *duk_push_lstring(duk_context *ctx, const char *str, duk_size_t len);
const char *duk_push_sprintf(duk_context *ctx, const char *fmt, ...) DUK_QJS_PRINTF(2, 3);
void duk_push_pointer(duk_context *ctx, void *p);
duk_idx_t duk_push_object(duk_context *ctx);
duk_idx_t duk_push_array(duk_context *ctx);
void duk_push_this(duk_context *ctx);
void duk_push_global_object(duk_context *ctx);
duk_idx_t duk_push_c_function(duk_context *ctx, duk_c_function func, duk_idx_t nargs);
void *duk_push_buffer(duk_context *ctx, duk_size_t size, duk_bool_t dynamic);
void duk_push_buffer_object(duk_context *ctx, duk_idx_t idx_buffer,
		duk_size_t byte_offset, duk_size_t byte_length, duk_uint_t flags);
duk_idx_t duk_push_error_object(duk_context *ctx, duk_errcode_t err_code,
		const char *fmt, ...) DUK_QJS_PRINTF(3, 4);
void duk_push_context_dump(duk_context *ctx);

/* type checks and reads */
duk_int_t duk_get_type(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_check_type(duk_context *ctx, duk_idx_t idx, duk_int_t type);
duk_bool_t duk_is_undefined(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_is_null(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_is_boolean(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_is_number(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_is_string(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_get_boolean(duk_context *ctx, duk_idx_t idx);
duk_int_t duk_get_int(duk_context *ctx, duk_idx_t idx);
duk_uint_t duk_get_uint(duk_context *ctx, duk_idx_t idx);
const char *duk_get_string(duk_context *ctx, duk_idx_t idx);
void *duk_get_pointer(duk_context *ctx, duk_idx_t idx);
duk_size_t duk_get_length(duk_context *ctx, duk_idx_t idx);
duk_bool_t duk_opt_boolean(duk_context *ctx, duk_idx_t idx, duk_bool_t def_value);
duk_bool_t duk_require_boolean(duk_context *ctx, duk_idx_t idx);
duk_int_t duk_require_int(duk_context *ctx, duk_idx_t idx);
duk_double_t duk_require_number(duk_context *ctx, duk_idx_t idx);
const char *duk_require_string(duk_context *ctx, duk_idx_t idx);
const char *duk_require_lstring(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len);
duk_bool_t duk_strict_equals(duk_context *ctx, duk_idx_t idx1, duk_idx_t idx2);

/* coercion (in place) */
duk_bool_t duk_to_boolean(duk_context *ctx, duk_idx_t idx);
duk_int_t duk_to_int(duk_context *ctx, duk_idx_t idx);
duk_uint_t duk_to_uint(duk_context *ctx, duk_idx_t idx);
duk_uint32_t duk_to_uint32(duk_context *ctx, duk_idx_t idx);
const char *duk_to_string(duk_context *ctx, duk_idx_t idx);
const char *duk_to_lstring(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len);
const char *duk_safe_to_lstring(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len);
#define duk_safe_to_string(ctx, idx) duk_safe_to_lstring((ctx), (idx), NULL)
const char *duk_safe_to_stacktrace(duk_context *ctx, duk_idx_t idx);
void duk_concat(duk_context *ctx, duk_idx_t count);

/* properties */
duk_bool_t duk_get_prop(duk_context *ctx, duk_idx_t obj_idx);
duk_bool_t duk_get_prop_string(duk_context *ctx, duk_idx_t obj_idx, const char *key);
duk_bool_t duk_get_prop_index(duk_context *ctx, duk_idx_t obj_idx, duk_uarridx_t arr_idx);
duk_bool_t duk_put_prop(duk_context *ctx, duk_idx_t obj_idx);
duk_bool_t duk_put_prop_string(duk_context *ctx, duk_idx_t obj_idx, const char *key);
duk_bool_t duk_put_prop_index(duk_context *ctx, duk_idx_t obj_idx, duk_uarridx_t arr_idx);
duk_bool_t duk_del_prop(duk_context *ctx, duk_idx_t obj_idx);
duk_bool_t duk_del_prop_string(duk_context *ctx, duk_idx_t obj_idx, const char *key);
duk_bool_t duk_del_prop_index(duk_context *ctx, duk_idx_t obj_idx, duk_uarridx_t arr_idx);
duk_bool_t duk_has_prop(duk_context *ctx, duk_idx_t obj_idx);
void duk_def_prop(duk_context *ctx, duk_idx_t obj_idx, duk_uint_t flags);
duk_bool_t duk_get_global_string(duk_context *ctx, const char *key);
duk_bool_t duk_put_global_string(duk_context *ctx, const char *key);
void duk_get_prototype(duk_context *ctx, duk_idx_t idx);
void duk_set_prototype(duk_context *ctx, duk_idx_t idx);
void duk_set_finalizer(duk_context *ctx, duk_idx_t idx);

/* calls and compiling */
void duk_call(duk_context *ctx, duk_idx_t nargs);
duk_int_t duk_pcall(duk_context *ctx, duk_idx_t nargs);
duk_int_t duk_pcall_method(duk_context *ctx, duk_idx_t nargs);
duk_int_t duk_safe_call(duk_context *ctx, duk_safe_call_function func,
		void *udata, duk_idx_t nargs, duk_idx_t nrets);
duk_int_t duk_pcompile(duk_context *ctx, duk_uint_t flags);
duk_int_t duk_pcompile_lstring_filename(duk_context *ctx, duk_uint_t flags,
		const char *src, duk_size_t len);

/* errors: these do not return */
duk_ret_t duk_error(duk_context *ctx, duk_errcode_t err_code,
		const char *fmt, ...) DUK_QJS_NORETURN DUK_QJS_PRINTF(3, 4);
duk_ret_t duk_generic_error(duk_context *ctx, const char *fmt, ...)
		DUK_QJS_NORETURN DUK_QJS_PRINTF(2, 3);

#endif
