/* xref.c -- does merely LINKING Xlib break the program, or only calling it?
 *
 * It references XOpenDisplay through a function pointer so the linker must
 * extract that member from libX11.a, and never calls it.  An earlier control
 * passed -lX11 without referring to any symbol and pulled in nothing at all,
 * because a static archive yields only the members something refers to -- so
 * it proved just that an unused -l is free.
 *
 * Against the vendor's unrepaired archives this segfaults before main, which
 * is how the sh_link defect was localised.  Against archives repaired by
 * tools/fix-x-libraries.sh it prints its two markers and exits 0.
 * See docs/TOOLCHAIN.md. */
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#define MARK(s) write(1, "[" s "]\n", (unsigned)strlen(s) + 3)
extern void *XOpenDisplay();
void *(*keep)() = XOpenDisplay;      /* forces the member in, no call */
int main(void)
{
	char b[64];
	MARK("main reached with Xlib linked in");
	sprintf(b, "XOpenDisplay is at %p\n", (void *)keep);
	write(1, b, (unsigned)strlen(b));
	MARK("done, never called it");
	return 0;
}
