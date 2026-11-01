/* zzq_m18.c -- the window code's zzgl_platform calls (renamed by the Makefile) as
   plain GLX, for a GLQuake on any libGL: Mesa 18 with zz3660 here.  The driver
   follows the window; the sound stays zzgl's, from libGL. */
#include <stdio.h>
#include <GL/glx.h>

static Display *d;
static Window w;
static GLXContext c;

void *zq_getproc(const char *name) { return (void *)glXGetProcAddressARB((const GLubyte *)name); }

int zq_open(void *dpy, unsigned long win, int x, int y, int width, int height)
{
	static int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 1, None };
	XVisualInfo *v;

	d = dpy;
	w = win;
	if (!(v = glXChooseVisual(d, DefaultScreen(d), attr)) || !(c = glXCreateContext(d, v, NULL, True)))
		return -1;
	return glXMakeCurrent(d, w, c) ? 0 : -1;
}

void zq_swap(void) { glXSwapBuffers(d, w); }
void zq_frame(void) { }
void zq_place(int x, int y) { }
void zq_close(void)
{
	if (c) {
		glXMakeCurrent(d, None, NULL);
		glXDestroyContext(d, c);
	}
	c = NULL;
}
