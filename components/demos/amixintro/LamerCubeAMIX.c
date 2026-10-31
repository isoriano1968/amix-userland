/*
 * LamerCubeAMIX -- an oldskool intro for AMIX on the A3000 + Z3660.
 *
 * Copper bars, a starfield, a spinning cube with the AMIX logo and a sine
 * scroller in the LamerIntroLinux bitmap font, to a module played by
 * mikmod.  Plain C and OpenGL 1.1 through Mesa 3.1 + GLUT; no blitter or
 * copper was harmed.
 *
 *   lamercubeamix [module]     ESC or q quits (and stops the music)
 *
 * The module defaults to /opt/lamercubeamix/paranoimia.mod; mikmod runs as a child
 * process (mikmod -q), so the picture never waits for the sound.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <GL/glut.h>

#include "font.h"
#include "logo.h"

#define WIN_W   512
#define WIN_H   320
#define NSTARS  120

static const char *scrolltext =
    "              HELLO AMIGA FREAKS!   WELCOME TO THE FIRST "
    "OLDSKOOL INTRO ON AMIX - COMMODORE'S UNIX FOR THE AMIGA, 1991.   "
    "RUNNING ON AN A3000 WITH A Z3660 AND A REAL 68060 AT 80 MHZ, "
    "X11R6.3 ON THE RTG CARD, MESA 3.1 DOING THE 3D IN SOFTWARE AND "
    "MIKMOD SENDING THE TUNE THROUGH PAULA...   NO BLITTER OR COPPER "
    "WAS HARMED DURING THIS PRODUCTION!   GREETINGS TO ALL AMIX "
    "ENTHUSIASTS OUT THERE...   PRESS ESC TO QUIT.         ";

/* the font strip: x position and width of each character from ' ' up
   (LamerIntroLinux's tables) */
static const int startList[] = {622,651,661,676,702,0,0,0,0,0,952,0,968,0,0,0,
    723,747,759,783,805,830,854,878,903,928,0,0,0,0,0,985,0,0, 26,50,72,96,117,
    138,162,186,195,213,239,259,289,315,339,363,389,413,436,459,482,507,546,572,
    598};
static const int widthList[] = { 28, 7,12,23,19,0,0,0,0,0, 14,0, 7,0,0,0,
    22, 9,22,19,22,22,22,22,22,22,0,0,0,0,0,20, 0,25,23,21,23,20,20,23,23, 8,
    17,25,19,29,25,23,23,25,23,22,22,22,24,38,25,25,22};
#define NCHARS ((int)(sizeof widthList / sizeof widthList[0]))

static GLuint texFont, texLogo;
static float starX[NSTARS], starY[NSTARS], starZ[NSTARS];
static float scrollX = WIN_W;
static int textWidth;
static pid_t music = -1;
static int frames, fpsTime;
static float fps;

/* test switches: LAMERCUBE_PARTS=copper,stars,cube,scroll (default all) and
   LAMERCUBE_SECONDS=n (quit after n seconds, print the per-part times) */
static int doCopper = 1, doStars = 1, doCube = 1, doScroll = 1, runSeconds;
static double partMs[5];
static int timedFrames;

static int glyph(int c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    c -= ' ';
    if (c < 0 || c >= NCHARS || widthList[c] == 0)
        return 0;               /* unknown: a blank */
    return c;
}

static int charWidth(int c)
{
    c = glyph(c);
    return c ? widthList[c] + 2 : 16;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)rand() / (float)RAND_MAX;
}

static void newStar(int i, int far)
{
    starX[i] = frand(-8.0f, 8.0f);
    starY[i] = frand(-5.0f, 5.0f);
    starZ[i] = far ? -30.0f : frand(-30.0f, -1.0f);
}

static void startMusic(const char *module)
{
    if (access(module, R_OK) != 0) {
        fprintf(stderr, "lamercubeamix: no module %s, no music\n", module);
        return;
    }
    music = fork();
    if (music == 0) {
        /* quiet mode: no interface, just the sound */
        execlp("mikmod", "mikmod", "-q", module, (char *) 0);
        _exit(1);
    }
}

static void stopMusic(void)
{
    if (music > 0) {
        kill(music, SIGTERM);
        waitpid(music, (int *) 0, 0);
        music = -1;
    }
}

static void quit(void)
{
    stopMusic();
    if (fpsTime > 0)
        fprintf(stderr, "lamercubeamix: %.1f fps\n", fps);
    if (timedFrames > 0)
        fprintf(stderr, "lamercubeamix: ms per frame: copper %.0f stars %.0f cube %.0f "
            "scroller %.0f swap %.0f (%d frames)\n",
            partMs[0] / timedFrames, partMs[1] / timedFrames, partMs[2] / timedFrames,
            partMs[3] / timedFrames, partMs[4] / timedFrames, timedFrames);
    exit(0);
}

static int lap(int *t0)
{
    int now, d;
    glFinish();
    now = glutGet(GLUT_ELAPSED_TIME);
    d = now - *t0;
    *t0 = now;
    return d;
}

static void init(void)
{
    int i;
    const char *p;

    glGenTextures(1, &texFont);
    glBindTexture(GL_TEXTURE_2D, texFont);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_W, FONT_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, font_data);

    glGenTextures(1, &texLogo);
    glBindTexture(GL_TEXTURE_2D, texLogo);
    /* nearest: Mesa's bilinear path in software is several times slower */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, LOGO_W, LOGO_H, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, logo_data);

    for (i = 0; i < NSTARS; i++)
        newStar(i, 0);
    for (textWidth = 0, p = scrolltext; *p; p++)
        textWidth += charWidth(*p);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
}

/* 2D: pixel coordinates, origin bottom left */
static void ortho(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIN_W, 0, WIN_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void perspective(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (double) WIN_W / WIN_H, 0.5, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* three copper bars bouncing on sine curves */
static void drawCopper(float t)
{
    static const float col[3][3] = {
        {1.0f, 0.2f, 0.2f}, {0.2f, 1.0f, 0.3f}, {0.3f, 0.4f, 1.0f}
    };
    int b;

    ortho();
    glBegin(GL_QUADS);
    for (b = 0; b < 3; b++) {
        float y = WIN_H * 0.62f + sin(t * 1.7f + b * 1.1f) * 90.0f;
        const float *c = col[b];
        glColor3f(0, 0, 0);       glVertex2f(0, y - 12);
        glColor3f(0, 0, 0);       glVertex2f(WIN_W, y - 12);
        glColor3f(c[0], c[1], c[2]); glVertex2f(WIN_W, y);
        glColor3f(c[0], c[1], c[2]); glVertex2f(0, y);
        glColor3f(c[0], c[1], c[2]); glVertex2f(0, y);
        glColor3f(c[0], c[1], c[2]); glVertex2f(WIN_W, y);
        glColor3f(0, 0, 0);       glVertex2f(WIN_W, y + 12);
        glColor3f(0, 0, 0);       glVertex2f(0, y + 12);
    }
    glEnd();
}

static void drawStars(float dt)
{
    int i;

    perspective();
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (i = 0; i < NSTARS; i++) {
        float b;
        starZ[i] += dt * 8.0f;
        if (starZ[i] > -1.0f)
            newStar(i, 1);
        b = 1.0f + starZ[i] / 30.0f;        /* near stars are brighter */
        glColor3f(b, b, b);
        glVertex3f(starX[i], starY[i], starZ[i]);
    }
    glEnd();
}

/* the cube, the AMIX logo on every face */
static void drawCube(float t)
{
    static const float n[6][3] = {
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
    };
    static const float v[6][4][3] = {
        {{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}},
        {{ 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}},
        {{ 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}},
        {{-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}},
        {{-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1}},
        {{-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1}}
    };
    static const float tc[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    int f, k;

    perspective();
    glTranslatef(0.0f, 0.6f, -6.5f + sin(t * 0.8f) * 0.8f);
    glRotatef(t * 50.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(t * 70.0f, 0.0f, 1.0f, 0.0f);
    glRotatef(t * 30.0f, 0.0f, 0.0f, 1.0f);

    /* a cube is convex: culling the back faces replaces the depth buffer */
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texLogo);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    for (f = 0; f < 6; f++) {
        /* cheap flat lighting from the viewer's side: the faces stay apart */
        float l = 0.55f + 0.45f * (float) fabs(n[f][2] * 0.6f + n[f][1] * 0.4f + n[f][0] * 0.5f);
        glColor3f(l, l, l);
        for (k = 0; k < 4; k++) {
            glTexCoord2f(tc[k][0], tc[k][1]);
            glVertex3f(v[f][k][0], v[f][k][1], v[f][k][2]);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
}

/* the sine scroller in the bitmap font */
static void drawScroller(float t, float dt)
{
    const char *p;
    float x;

    ortho();
    glEnable(GL_TEXTURE_2D);
    /* alpha test, not blending: no read-back of every pixel */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glBindTexture(GL_TEXTURE_2D, texFont);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(1.0f, 1.0f, 1.0f);

    scrollX -= dt * 110.0f;
    if (scrollX < -textWidth)
        scrollX = WIN_W;

    glBegin(GL_QUADS);
    for (x = scrollX, p = scrolltext; *p && x < WIN_W; p++) {
        int g = glyph(*p), w = charWidth(*p);
        if (g && x + w > 0) {
            float s0 = (float) startList[g] / FONT_W;
            float s1 = (float) (startList[g] + widthList[g]) / FONT_W;
            float y = 40.0f + sin(t * 3.0f + x * 0.012f) * 22.0f;
            glTexCoord2f(s0, 0.0f); glVertex2f(x, y);
            glTexCoord2f(s1, 0.0f); glVertex2f(x + widthList[g], y);
            glTexCoord2f(s1, 1.0f); glVertex2f(x + widthList[g], y + 32);
            glTexCoord2f(s0, 1.0f); glVertex2f(x, y + 32);
        }
        x += w;
    }
    glEnd();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
}

/*
 * Xzz9000 copies a MIT-SHM swap with move16 (14 ms for 512x320 instead of
 * 34-59) when the window's root x is a multiple of 8 pixels.  The intro
 * moves itself there over its first frames: one test move shows where the
 * window manager puts the client area for a requested position (frame and
 * title), the second asks for the one that lands it aligned.
 */
static void alignWindow(int frame)
{
    static int x0, y0;
    int x, y;

    if (frame == 5) {
        x0 = glutGet(GLUT_WINDOW_X);
        y0 = glutGet(GLUT_WINDOW_Y);
        if (x0 % 8 != 0)
            glutPositionWindow(x0, y0);
    } else if (frame == 15 && x0 % 8 != 0) {
        x = glutGet(GLUT_WINDOW_X);
        y = glutGet(GLUT_WINDOW_Y);
        glutPositionWindow(x0 - (x - x0) - x0 % 8, y0 - (y - y0));
    } else if (frame == 25 && runSeconds > 0) {
        fprintf(stderr, "lamercubeamix: window at x=%d%s\n", glutGet(GLUT_WINDOW_X),
                glutGet(GLUT_WINDOW_X) % 8 ? "" : " (aligned)");
    }
}

static void display(void)
{
    static int frame;
    static int last = -1;
    int now = glutGet(GLUT_ELAPSED_TIME);
    float t = now / 1000.0f;
    float dt = last < 0 ? 0.0f : (now - last) / 1000.0f;

    if (dt > 0.25f)
        dt = 0.25f;             /* a stall must not throw everything away */
    last = now;

    {
        int t0 = now;
        glClear(GL_COLOR_BUFFER_BIT);
        if (doCopper) drawCopper(t);
        partMs[0] += lap(&t0);
        if (doStars) drawStars(dt);
        partMs[1] += lap(&t0);
        if (doCube) drawCube(t);
        partMs[2] += lap(&t0);
        if (doScroll) drawScroller(t, dt);
        partMs[3] += lap(&t0);
        glutSwapBuffers();
        partMs[4] += lap(&t0);
        if (frame < 30)
            alignWindow(++frame);
        timedFrames++;
    }
    if (runSeconds > 0 && now >= runSeconds * 1000)
        quit();

    frames++;
    if (now - fpsTime >= 5000) {
        fps = frames * 1000.0f / (now - fpsTime);
        frames = 0;
        fpsTime = now;
    }
}

static void idle(void)
{
    glutPostRedisplay();
}

static void key(unsigned char k, int x, int y)
{
    if (k == 27 || k == 'q' || k == 'Q')
        quit();
}

int main(int argc, char **argv)
{
    const char *module = "/opt/lamercubeamix/paranoimia.mod";

    glutInit(&argc, argv);
    {
        char *e = getenv("LAMERCUBE_PARTS");
        if (e) {
            doCopper = strstr(e, "copper") != 0;
            doStars = strstr(e, "stars") != 0;
            doCube = strstr(e, "cube") != 0;
            doScroll = strstr(e, "scroll") != 0;
        }
        e = getenv("LAMERCUBE_SECONDS");
        if (e)
            runSeconds = atoi(e);
    }
    if (argc > 1)
        module = argv[1];
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);       /* no depth buffer */
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("LamerCubeAMIX");
    init();
    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutKeyboardFunc(key);
    startMusic(module);
    fpsTime = glutGet(GLUT_ELAPSED_TIME);
    glutMainLoop();
    return 0;
}
