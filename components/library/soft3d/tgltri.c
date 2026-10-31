/* tgltri.c -- the 060 side of soft3dtest's comparison: the same Gouraud,
 * Z-tested triangle (160,20)-(300,220)-(20,220) in a 320x240 16-bit buffer,
 * n times, with TinyGL's off-screen context in ordinary (copyback) RAM.
 * (2026-10-26)
 *
 *   tgltri [n]
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/oscontext.h>

#define W 320
#define H 240

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* pixel coordinates to clip space, as the viewport maps them back */
static void vtx(float x, float y)
{
    glVertex3f(x / (W / 2) - 1.0f, 1.0f - y / (H / 2), 0.0f);
}

static void tri(void)
{
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); vtx(160, 20);
    glColor3f(0, 1, 0); vtx(300, 220);
    glColor3f(0, 0, 1); vtx(20, 220);
    glEnd();
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 500, i, lit = 0;
    unsigned short *fb = calloc(W * H, 2);
    void *fbs[1];
    ostgl_context *c;
    double t0, t;

    fbs[0] = fb;
    c = ostgl_create_context(W, H, 16, fbs, 1);
    ostgl_make_current(c, 0);
    glViewport(0, 0, W, H);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    tri();
    for (i = 0; i < W * H; i++)
        if (fb[i])
            lit++;
    printf("pixels drawn: %d, centre 0x%04x, near red corner 0x%04x\n", lit,
           fb[130 * W + 160], fb[30 * W + 160]);

    t0 = now();
    for (i = 0; i < n; i++)
        tri();
    t = now() - t0;
    printf("TinyGL on the 060: %d triangles: %.2f s = %.0f tri/s, %.1f Mpixel/s\n",
           n, t, n / t, n * 28000.0 / t / 1e6);
    return 0;
}
