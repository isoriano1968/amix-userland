/*
 * blendtest.c -- TinyGL blending on soft3D (2026-10-26).  One frame shows:
 * an opaque checkered floor; three overlapping 50% quads (SRC_ALPHA /
 * ONE_MINUS_SRC_ALPHA, depth mask off); additive particles (ONE / ONE);
 * a ring cut out of an RGBA texture by the alpha test; a cube textured
 * with MODULATE under a tint.  The 68k rasterizer has no blending: run
 * on soft3D (the default).  Prints fps every 5 s; BLENDTEST_SECONDS ends it,
 * BLENDTEST_STILL=1 stops the motion.
 *
 *   blendtest [-geometry WxH+X+Y]
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <GL/gl.h>
#include <GL/glut.h>

#define NPART 64

static GLuint texFloor, texRing;
static int frames, lastReport, seconds;
static float partX[NPART], partY[NPART], partV[NPART];

static void makeTextures(void)
{
    static unsigned char floor[64 * 64 * 3], ring[64 * 64 * 4];
    int x, y;

    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            unsigned char *p = &floor[(y * 64 + x) * 3];
            int on = ((x >> 3) ^ (y >> 3)) & 1;
            p[0] = on ? 200 : 40; p[1] = on ? 200 : 40; p[2] = on ? 220 : 70;
        }
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            unsigned char *p = &ring[(y * 64 + x) * 4];
            float dx = x - 31.5f, dy = y - 31.5f, r = (float) sqrt(dx * dx + dy * dy);
            p[0] = 255; p[1] = (unsigned char) (x * 4); p[2] = (unsigned char) (y * 4);
            p[3] = (r > 18.0f && r < 30.0f) ? 255 : 0;
        }
    glGenTextures(1, &texFloor);
    glBindTexture(GL_TEXTURE_2D, texFloor);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, floor);
    glGenTextures(1, &texRing);
    glBindTexture(GL_TEXTURE_2D, texRing);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, ring);
}

static void quad(float x0, float y0, float x1, float y1, float z)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(x0, y0, z);
    glTexCoord2f(1, 0); glVertex3f(x1, y0, z);
    glTexCoord2f(1, 1); glVertex3f(x1, y1, z);
    glTexCoord2f(0, 1); glVertex3f(x0, y1, z);
    glEnd();
}

static void cube(void)
{
    static const float v[6][4][3] = {
        {{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}},
        {{ 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}},
        {{ 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}},
        {{-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}},
        {{-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1}},
        {{-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1}}
    };
    static const float tc[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    int f, k;

    glBegin(GL_QUADS);
    for (f = 0; f < 6; f++)
        for (k = 0; k < 4; k++) {
            glTexCoord2f(tc[k][0], tc[k][1]);
            glVertex3f(v[f][k][0], v[f][k][1], v[f][k][2]);
        }
    glEnd();
}

static void display(void)
{
    float t = getenv("BLENDTEST_STILL") ? 1.0f : glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    int i, now;

    glClearColor(0.05f, 0.05f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -0.5f, -9.0f);

    /* opaque: the floor and the tinted, modulated cube */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texFloor);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(1.0f, 1.0f, 1.0f);
    glPushMatrix();
    glRotatef(-70.0f, 1.0f, 0.0f, 0.0f);
    quad(-6, -6, 6, 6, -1.5f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(2.8f, 0.8f, -1.0f);
    glRotatef(t * 40.0f, 1.0f, 1.0f, 0.0f);
    glColor3f(1.0f, 0.5f, 0.2f);                /* tint: MODULATE */
    cube();
    glPopMatrix();

    /* alpha test: the ring cut out of the RGBA texture */
    glBindTexture(GL_TEXTURE_2D, texRing);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glPushMatrix();
    glTranslatef(-2.8f, 0.8f, 0.0f);
    glRotatef(t * 60.0f, 0.0f, 1.0f, 0.0f);
    quad(-1.3f, -1.3f, 1.3f, 1.3f, 0.0f);
    glPopMatrix();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);

    /* translucent: three 50% quads, depth mask off */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glPushMatrix();
    glTranslatef((float) sin(t) * 0.5f, 0.8f, 0.5f);
    glColor4f(1.0f, 0.0f, 0.0f, 0.5f); quad(-1.4f, -0.6f, 0.2f, 1.0f, 0.0f);
    glColor4f(0.0f, 1.0f, 0.0f, 0.5f); quad(-0.8f, -1.2f, 0.8f, 0.4f, 0.1f);
    glColor4f(0.0f, 0.3f, 1.0f, 0.5f); quad(-0.2f, -0.6f, 1.4f, 1.0f, 0.2f);
    glPopMatrix();

    /* additive: particles rising */
    glBlendFunc(GL_ONE, GL_ONE);
    for (i = 0; i < NPART; i++) {
        float y = partY[i] + (getenv("BLENDTEST_STILL") ? 0.0f : t * partV[i]);
        y = (float) fmod(y, 4.0);
        glColor4f(0.6f, 0.3f, 0.1f, 1.0f);
        quad(partX[i] - 0.15f, y - 1.6f, partX[i] + 0.15f, y - 1.3f, 1.0f);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glutSwapBuffers();
    frames++;
    now = glutGet(GLUT_ELAPSED_TIME);
    if (now - lastReport >= 5000) {
        printf("%d frames in %.1f s = %.1f fps\n", frames, (now - lastReport) / 1000.0,
               frames * 1000.0 / (now - lastReport));
        fflush(stdout);
        frames = 0;
        lastReport = now;
    }
    if (seconds && now >= seconds * 1000)
        exit(0);
}

static void idle(void)
{
    glutPostRedisplay();
}

static void key(unsigned char k, int x, int y)
{
    if (k == 27 || k == 'q')
        exit(0);
}

int main(int argc, char **argv)
{
    int i;

    glutInit(&argc, argv);
    glutInitWindowSize(512, 320);
    glutCreateWindow("blendtest");
    if (getenv("BLENDTEST_SECONDS"))
        seconds = atoi(getenv("BLENDTEST_SECONDS"));
    for (i = 0; i < NPART; i++) {
        partX[i] = (float) (rand() % 600) / 100.0f - 3.0f;
        partY[i] = (float) (rand() % 400) / 100.0f;
        partV[i] = 0.5f + (float) (rand() % 100) / 100.0f;
    }
    makeTextures();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.625, 0.625, 1.5, 40.0);
    glShadeModel(GL_SMOOTH);
    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutKeyboardFunc(key);
    glutMainLoop();
    return 0;
}
