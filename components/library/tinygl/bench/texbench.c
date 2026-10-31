/* texbench.c -- one source, two OpenGLs: a spinning textured cube (256x256
 * texture, GL_NEAREST, perspective, depth test) and a flat-shaded lit
 * torus-free scene, 300x300, plain GLX and GL 1.1 only, so it links against
 * Mesa 3.1 (libGL.a) and TinyGL (libTinyGL.a) alike.  (2026-10-26)
 *
 *   texbench [seconds] [tex|flat]     default 20 tex; prints fps every 5 s
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void face(float nx, float ny, float nz, const float v[4][3])
{
    static const float tc[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    int i;
    glNormal3f(nx, ny, nz);
    for (i = 0; i < 4; i++) {
        glTexCoord2f(tc[i][0], tc[i][1]);
        glVertex3f(v[i][0], v[i][1], v[i][2]);
    }
}

static void cube(void)
{
    static const float f[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}} };
    static const float n[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    int i;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++)
        face(n[i][0], n[i][1], n[i][2], f[i]);
    glEnd();
}

int main(int argc, char **argv)
{
    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None };
    int secs = argc > 1 ? atoi(argv[1]) : 20, tex = !(argc > 2 && strcmp(argv[2], "flat") == 0);
    Display *dpy = XOpenDisplay(0);
    XVisualInfo *vi;
    XSetWindowAttributes swa;
    GLXContext cx;
    Window win;
    unsigned char *img;
    double t0, tr, t;
    int frames = 0, x, y;

    if (!dpy) { printf("no display\n"); return 1; }
    vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (!vi) { printf("no visual\n"); return 1; }
    cx = glXCreateContext(dpy, vi, 0, GL_TRUE);
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
    swa.border_pixel = 0;
    swa.override_redirect = True;   /* same place for both, no WM */
    win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 64, 64, 300, 300, 0, vi->depth,
                        InputOutput, vi->visual,
                        CWBorderPixel | CWColormap | CWOverrideRedirect, &swa);
    XMapWindow(dpy, win);
    XSync(dpy, False);
    glXMakeCurrent(dpy, win, cx);

    img = malloc(256 * 256 * 3);
    for (y = 0; y < 256; y++)
        for (x = 0; x < 256; x++) {
            unsigned char *p = img + (y * 256 + x) * 3;
            int c = ((x >> 5) ^ (y >> 5)) & 1;
            p[0] = c ? 255 : x;
            p[1] = c ? y : 64;
            p[2] = c ? 32 : 255 - x;
        }
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    if (tex)
        glEnable(GL_TEXTURE_2D);
    else {
        static const float pos[4] = { 2, 3, 5, 0 };
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glViewport(0, 0, 300, 300);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);

    t0 = tr = now();
    printf("%s, %d s\n", tex ? "textured cube" : "lit cube", secs);
    fflush(stdout);
    for (;;) {
        t = now();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0, 0, -4.2f);
        glRotatef((float)(t * 40.0), 1, 0, 0);
        glRotatef((float)(t * 57.0), 0, 1, 0);
        cube();
        glXSwapBuffers(dpy, win);
        frames++;
        if (t - tr >= 5.0) {
            printf("%d frames in %g seconds = %g FPS\n", frames, t - tr, frames / (t - tr));
            fflush(stdout);
            frames = 0;
            tr = t;
        }
        if (t - t0 >= secs)
            break;
    }
    return 0;
}
