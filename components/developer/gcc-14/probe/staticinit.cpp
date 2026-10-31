/* staticinit.cpp -- do C++ static constructors run at all?
 *
 * OpenTTD registers every video, sound and music driver, and every blitter, by
 * constructing a global object whose constructor inserts into a std::map.  If
 * those constructors never run, the maps are never constructed either, and the
 * first iteration walks a tree of zeroes -- which is exactly where OpenTTD
 * dies on AMIX: std::_Rb_tree_increment, with the crash log reporting
 * "Blitter: none" and "Video driver: none".
 *
 * That is a plausible failure here rather than a strange one.  SVR4.0 predates
 * .init_array by years; its crt1.o knows only the .init section.  If GCC
 * registers constructors the modern way and nothing walks that array, they are
 * silently skipped -- no diagnostic, no link error, just a program whose
 * globals are all zero.
 *
 * Prints from a constructor, from main, and from a destructor, so the answer
 * distinguishes "never ran", "ran late" and "ran but did not tear down".
 */
#include <stdio.h>

static int constructed;

struct Early {
	Early()  { constructed = 1; printf("1 constructor ran before main\n"); fflush(stdout); }
	~Early() { printf("4 destructor ran after main\n"); fflush(stdout); }
};

static Early early;

/* A second one, to show whether ordering within a translation unit holds. */
struct Second {
	Second() { printf("2 second constructor ran\n"); fflush(stdout); }
};
static Second second;

int main(void)
{
	printf("3 main: constructed=%d (want 1)\n", constructed);
	fflush(stdout);
	return constructed ? 0 : 1;
}
