/*
 * The _init of a C++ extension module for AMIX, between begin.s and end.s
 * (files/amix-cc puts them around the link).  AMIX's ld.so.1 (1991) runs a
 * library's DT_INIT after relocating it, but knows nothing of .init_array,
 * where gcc 14 puts constructors (numpy's and the static libstdc++'s), and
 * gcc's -shared link adds no crtbegin.o, which registers the exception
 * frames of a program.  So this registers the module's .eh_frame with the
 * unwinder (a C++ exception thrown and caught inside the module then
 * unwinds instead of calling terminate) and runs the constructors, as
 * tools/so-init does for libraries without exceptions.
 *
 * __dso_handle and __cxa_atexit are what crtbeginS.o and libc give elsewhere;
 * python3 never unloads an extension module, so destructors registered at
 * construction never need to run.  All hidden: each module keeps its own.
 */
#define HIDDEN __attribute__((visibility("hidden")))

struct object { void *pc_begin, *tbase, *dbase, *u, *s, *next; };	/* libgcc's, 24 bytes */
extern void __register_frame_info(const void *, struct object *);

extern char __amix_so_eh_frame[] HIDDEN;
extern void (*__amix_so_init_array[])(void) HIDDEN;
extern void (*__amix_so_init_array_end[])(void) HIDDEN;

void *__dso_handle HIDDEN = &__dso_handle;

HIDDEN int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
	(void)fn; (void)arg; (void)dso;
	return 0;
}

void _init(void)
{
	static struct object ob;
	void (**p)(void);

	__register_frame_info(__amix_so_eh_frame, &ob);
	for (p = __amix_so_init_array; p < __amix_so_init_array_end; p++)
		(*p)();
}
