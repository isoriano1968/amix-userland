/*
 * The .init of a shared library with constructors.  AMIX's ld.so.1 (1991)
 * calls a library's DT_INIT, after relocating it, but knows nothing of
 * .init_array, where gcc 14 puts constructors; and gcc's -shared startfiles
 * give a library no _init.  This one walks the array between begin.s and
 * end.s, whose labels the shared linker script, unlike the program's, does
 * not provide.  Tested 2026-11-03: a constructor's store is seen by the
 * library and, through R_68K_COPY, by the program.
 */
extern void (*__amix_so_init_array[])(void) __attribute__((visibility("hidden")));
extern void (*__amix_so_init_array_end[])(void) __attribute__((visibility("hidden")));

void _init(void)
{
	void (**p)(void);

	for (p = __amix_so_init_array; p < __amix_so_init_array_end; p++)
		(*p)();
}
