/* amix-compat.c -- what Mesa 3.1's glut, built on AMIX with the box's own
 * gcc 2.7, expects from that gcc's libgcc: __eprintf, assert()'s reporter
 * (gcc 2.x <assert.h> calls it; the cross toolchain's libgcc has none). */
#include <stdio.h>
#include <stdlib.h>

void
__eprintf(const char *fmt, const char *file, unsigned int line, const char *expr)
{
    fprintf(stderr, fmt, file, line, expr);
    fflush(stderr);
    abort();
}
