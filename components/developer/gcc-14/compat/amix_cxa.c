/* amix_cxa.c -- the C++ ABI entry points AMIX's libc predates.
 *
 * The Itanium C++ ABI says the C library provides __cxa_atexit: the compiler
 * emits a call to it for every object with static storage duration and a
 * destructor, and the runtime calls those destructors at exit in reverse order.
 * AMIX is SVR4.0 and knows only atexit(void (*)(void)), which cannot carry the
 * object pointer, so libstdc++ does not link:
 *
 *     system_error.cc:145: undefined reference to `__cxa_atexit'
 *
 * The alternative was to configure GCC with --disable-__cxa_atexit, which makes
 * it fall back to the older scheme. This is preferred because the gap is in the
 * C library, not in the compiler: filling it here keeps the compiler standard,
 * and anything else built with it gets working static destructors too.
 *
 * The list grows on demand rather than being a fixed table, because silently
 * dropping a destructor past some arbitrary count is the kind of bug that only
 * shows up as a file not flushed on a machine nobody can attach a debugger to.
 */
#include <stdlib.h>

typedef void (*amix_cxa_func)(void *);

struct amix_cxa_entry {
	amix_cxa_func func;
	void *arg;
	void *dso;
};

static struct amix_cxa_entry *amix_cxa_list;
static unsigned amix_cxa_count;
static unsigned amix_cxa_capacity;
static int amix_cxa_hooked;

/* Address of this translation unit, for the ABI's dso argument. Normally
 * supplied by crtbegin; defined weakly here so a toolchain that does provide
 * one wins. */
void *__dso_handle __attribute__((weak)) = &__dso_handle;

void __cxa_finalize(void *dso)
{
	/* Reverse order, and re-read the count each time: a destructor is
	 * allowed to register more work. */
	while (amix_cxa_count > 0) {
		struct amix_cxa_entry e = amix_cxa_list[--amix_cxa_count];
		if (dso == 0 || e.dso == dso) e.func(e.arg);
	}
}

static void amix_cxa_run_all(void)
{
	__cxa_finalize(0);
}

int __cxa_atexit(amix_cxa_func func, void *arg, void *dso)
{
	if (func == 0) return -1;

	if (!amix_cxa_hooked) {
		if (atexit(amix_cxa_run_all) != 0) return -1;
		amix_cxa_hooked = 1;
	}

	if (amix_cxa_count == amix_cxa_capacity) {
		unsigned grown = amix_cxa_capacity ? amix_cxa_capacity * 2 : 32;
		struct amix_cxa_entry *bigger = (struct amix_cxa_entry *)
			realloc(amix_cxa_list, grown * sizeof(*bigger));
		if (bigger == 0) return -1;
		amix_cxa_list = bigger;
		amix_cxa_capacity = grown;
	}

	amix_cxa_list[amix_cxa_count].func = func;
	amix_cxa_list[amix_cxa_count].arg = arg;
	amix_cxa_list[amix_cxa_count].dso = dso;
	amix_cxa_count++;
	return 0;
}
