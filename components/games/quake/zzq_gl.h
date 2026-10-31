/* zzq_gl.h -- GLQuake's GL calls on the Z3660's core 1: zzgl hands its
   functions out by name only, so each gl* here is a pointer (zzq_gl.c). */
#ifndef ZZQ_GL_H
#define ZZQ_GL_H

#define ZQ_PROCS \
	P(glAlphaFunc) P(glBegin) P(glBindTexture) P(glBlendFunc) P(glClear) P(glClearColor) \
	P(glColor3f) P(glColor4f) P(glColor4fv) P(glCullFace) P(glDepthFunc) P(glDepthMask) \
	P(glDepthRange) P(glDisable) P(glDrawBuffer) P(glEnable) P(glEnd) P(glFinish) P(glFlush) \
	P(glFogf) P(glFogfv) P(glFogi) P(glFrustum) P(glGetFloatv) P(glGetString) P(glHint) \
	P(glLoadIdentity) P(glLoadMatrixf) P(glMatrixMode) P(glOrtho) P(glPolygonMode) \
	P(glPopMatrix) P(glPushMatrix) P(glReadBuffer) P(glReadPixels) P(glRotatef) P(glScalef) \
	P(glShadeModel) P(glTexCoord2f) P(glTexCoord2fv) P(glTexEnvf) P(glTexImage2D) \
	P(glTexParameterf) P(glTexSubImage2D) P(glTranslatef) P(glVertex2f) P(glVertex3f) \
	P(glVertex3fv) P(glViewport)

#define P(n) extern __typeof__(n) *zq_##n;
ZQ_PROCS
#undef P
#include "zzq_names.h"

void zq_glColor3ubv(const GLubyte *v);
#define glColor3ubv zq_glColor3ubv
void zq_gl_init(void);

#endif
