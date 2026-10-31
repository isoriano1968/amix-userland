/* cxxprobe.cpp -- the four C++ features OpenTTD 1.0.5 cannot do without, asked
 * one at a time so a failure names itself.
 *
 * Order is deliberate: each step needs more of the C++ runtime than the last.
 *   1. templates and inlining   -- compiler only, no runtime at all
 *   2. virtual dispatch         -- needs a vtable and __cxa_pure_virtual
 *   3. new / delete             -- needs operator new from libsupc++
 *   4. throw / catch            -- needs the unwinder, and is the one genuine
 *                                  unknown: DWARF unwinding normally locates
 *                                  its tables through the dynamic loader, and
 *                                  AMIX's loader knows nothing about them.  The
 *                                  toolchain is built so that crtbegin registers
 *                                  them from .init instead; this is where that
 *                                  either works or does not.
 *
 * Every line printed is flushed before the next step runs, so if the process
 * dies the last line tells you which step it died in.
 */
#include <stdio.h>

/* 1. templates */
template <typename T> static T maximum(T a, T b) { return a > b ? a : b; }

/* 2. virtual dispatch */
struct Shape {
	virtual ~Shape() {}
	virtual int area() const = 0;
};
struct Square : Shape {
	int side;
	explicit Square(int s) : side(s) {}
	virtual int area() const { return side * side; }
};

/* 4. the exception OpenTTD's savegame loader throws */
struct LoadError {
	const char *why;
	explicit LoadError(const char *w) : why(w) {}
};

static void deep(int depth)
{
	if (depth == 0) throw LoadError("unwound from four frames down");
	deep(depth - 1);
}

int main(void)
{
	printf("1 templates:  maximum(3,7)=%d maximum(2.5,1.5)=%.1f\n",
	       maximum(3, 7), maximum(2.5, 1.5));
	fflush(stdout);

	{
		Square s(6);
		const Shape &r = s;
		printf("2 virtual:    area=%d (want 36)\n", r.area());
		fflush(stdout);
	}

	{
		Shape *p = new Square(9);
		printf("3 new/delete: area=%d (want 81)\n", p->area());
		fflush(stdout);
		delete p;
		printf("3 new/delete: destructor returned\n");
		fflush(stdout);
	}

	printf("4 throw/catch: about to throw from four frames down\n");
	fflush(stdout);
	try {
		deep(4);
		printf("4 throw/catch: FAILED -- no exception arrived\n");
		return 1;
	} catch (const LoadError &e) {
		printf("4 throw/catch: caught, why=\"%s\"\n", e.why);
	}
	fflush(stdout);

	printf("all four ok\n");
	return 0;
}
