/* zzq_gl.c -- the pointers of zzq_gl.h, from zzgl */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include "zzq_gl.h"
#include "zzgl_enc.h"

#define P(n) __typeof__(zq_##n) zq_##n;
ZQ_PROCS
#undef P

void zq_glColor3ubv(const GLubyte *v)
{
	zq_glColor3f(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f);
}

void zq_gl_init(void)
{
#define P(n) \
	if ((zq_##n = zzgl_GetProcAddress(#n)) == NULL) { \
		fprintf(stderr, "glquake: the GL has no %s\n", #n); \
		exit(1); \
	}
	ZQ_PROCS
#undef P
}
