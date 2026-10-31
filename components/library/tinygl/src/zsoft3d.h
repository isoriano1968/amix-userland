/* zsoft3d.h -- TinyGL's rasterizer on the Z3660's ARM (zsoft3d.c) */
#ifndef ZSOFT3D_H
#define ZSOFT3D_H
extern int s3d_active;
void s3d_triangle(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2);
void s3d_point(GLContext *c, GLVertex *p);
void s3d_line(GLContext *c, GLVertex *p1, GLVertex *p2);
void s3d_clear(int color, int depth, float r, float g, float b);
#ifdef _XLIB_H_
int  s3d_begin(Display *dpy, int w, int h);
void s3d_swap(Display *dpy, Drawable win);
#endif
#endif
