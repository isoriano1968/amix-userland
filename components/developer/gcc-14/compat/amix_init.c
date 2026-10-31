/* amix_init.c -- run the .init_array, which AMIX's 1991 startup knows nothing
 * about.
 *
 * THE SYMPTOM: no C++ static constructor runs.  Every global object is left
 * zeroed, and the first thing that touches one dies -- for OpenTTD that is
 * std::_Rb_tree_increment, walking a driver-registry std::map that was never
 * constructed, with the crash log helpfully reporting "Blitter: none" and
 * "Video driver: none".
 *
 * THE CAUSE is a disagreement about a section name, thirty years apart.  AMIX's
 * crt1.o calls _init, crti.o provides it, and GCC's crtbegin.o puts a fragment
 * there that walks .ctors -- so far so 1991.  But the modern linker script
 * folds .ctors from ordinary objects INTO .init_array:
 *
 *     KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o ...) .ctors))
 *
 * so the constructors land in .init_array, .ctors keeps only crtbegin's -1 and
 * crtend's 0, and __do_global_ctors_aux walks an empty list and returns.  On a
 * system whose crt1 knows about .init_array nobody notices, because its startup
 * walks that array too.  AMIX's does not, and there is no diagnostic anywhere:
 * it links, it runs, and the constructors are simply skipped.
 *
 * THE FIX is this object, placed in the startfile sequence right after
 * crtbegin.o, contributing its own .init fragment.  Ordering is why it is a
 * separate object named in the compiler's specs rather than a member of
 * libamixc99: a library member is pulled in only when something references it,
 * and nothing references a constructor.
 */

/* Provided by the linker script, and weak so this still links if a future
 * script stops emitting them. */
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));
extern void (*__fini_array_start[])(void) __attribute__((weak));
extern void (*__fini_array_end[])(void) __attribute__((weak));

void __amix_run_init_array(void)
{
	void (**p)(void);

	if (__init_array_start == 0) return;
	for (p = __init_array_start; p < __init_array_end; p++) {
		if (*p != 0) (**p)();
	}
}

void __amix_run_fini_array(void)
{
	void (**p)(void);

	if (__fini_array_start == 0) return;
	/* Reverse order, as destruction should be. */
	for (p = __fini_array_end; p > __fini_array_start; ) {
		p--;
		if (*p != 0) (**p)();
	}
}

/* The fragments themselves.  crti.o opens _init and _fini, crtn.o closes them,
 * and everything linked between contributes its body -- which is why this has
 * to sit inside that window and why the specs decide where. */
__asm__(
	"	.section .init,\"ax\",@progbits\n"
	"	jsr __amix_run_init_array\n"
	"	.section .fini,\"ax\",@progbits\n"
	"	jsr __amix_run_fini_array\n"
	"	.text\n"
);
