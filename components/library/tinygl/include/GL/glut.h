/*
 * glut.h -- a GLUT subset for TinyGL on AMIX (tinygl-amix/glut/glut.c).
 * (2026-10-26)  The calls a classic GLUT demo needs to open a window and run:
 * window setup, the display/idle/keyboard/special/reshape/timer callbacks,
 * swap, glutGet for time and geometry, window position and title.  No menus,
 * fonts, shapes or overlays.  One window.
 */
#ifndef TINYGL_GLUT_H
#define TINYGL_GLUT_H

#include <GL/gl.h>

/* display mode bits (accepted; TinyGL is always RGB, double, Z) */
#define GLUT_RGB            0
#define GLUT_RGBA           0
#define GLUT_SINGLE         0
#define GLUT_DOUBLE         2
#define GLUT_DEPTH          16

/* glutGet */
#define GLUT_WINDOW_X       100
#define GLUT_WINDOW_Y       101
#define GLUT_WINDOW_WIDTH   102
#define GLUT_WINDOW_HEIGHT  103
#define GLUT_SCREEN_WIDTH   200
#define GLUT_SCREEN_HEIGHT  201
#define GLUT_ELAPSED_TIME   700

/* special keys */
#define GLUT_KEY_F1         1
#define GLUT_KEY_F2         2
#define GLUT_KEY_F3         3
#define GLUT_KEY_F4         4
#define GLUT_KEY_F5         5
#define GLUT_KEY_F6         6
#define GLUT_KEY_F7         7
#define GLUT_KEY_F8         8
#define GLUT_KEY_F9         9
#define GLUT_KEY_F10        10
#define GLUT_KEY_LEFT       100
#define GLUT_KEY_UP         101
#define GLUT_KEY_RIGHT      102
#define GLUT_KEY_DOWN       103
#define GLUT_KEY_PAGE_UP    104
#define GLUT_KEY_PAGE_DOWN  105
#define GLUT_KEY_HOME       106
#define GLUT_KEY_END        107
#define GLUT_KEY_INSERT     108

void glutInit(int *argc, char **argv);
void glutInitDisplayMode(unsigned int mode);
void glutInitWindowSize(int width, int height);
void glutInitWindowPosition(int x, int y);
int  glutCreateWindow(const char *title);
void glutDisplayFunc(void (*func)(void));
void glutIdleFunc(void (*func)(void));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutSpecialFunc(void (*func)(int key, int x, int y));
void glutReshapeFunc(void (*func)(int width, int height));
void glutTimerFunc(unsigned int msecs, void (*func)(int value), int value);
void glutPostRedisplay(void);
void glutSwapBuffers(void);
int  glutGet(int what);
void glutPositionWindow(int x, int y);
void glutSetWindowTitle(const char *title);
void glutMainLoop(void);

#endif
