/*
 * LameCubeAMIGA42 -- the second oldskool intro for AMIX on the A3000 + Z3660.
 *
 * A starfield, the spinning AMIX logo cube, and a message whose letters fly
 * in from the back one by one, hold, and fly out past the viewer, page after
 * page.  The letters are built from blocks of LamerCubeAMIX's bitmap font
 * (4x4-pixel cells, merged into horizontal runs): a dozen flat, gradient
 * shaded quads per letter, no texture and no transparency, so the text costs
 * little raster time.  Rendered by TinyGL 0.4.1 straight into a MIT-SHM
 * image, through tinygl-amix's GLUT subset; mikmod plays the module as a
 * child process.
 *
 *   lamecubeamiga42 [module]     ESC or q quits (and stops the music)
 *
 * LAMECUBE_SECONDS=n quits after n seconds and prints the frame rate.
 * Tests: LAMECUBE_STILL=1 stops the cube, LAMECUBE_FACE=0..5 turns one face
 * to the viewer.
 * 
 * Greetings to my PiStorm colleagues you Linux fanboys ... Sun forever!
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
#define NSTARS  150
#define ZTEXT   (-12.0f)        /* where the letters land */
#define BLOCK   0.16f           /* one font cell (4x4 pixels) in world units */
#define FLY_IN  1.2f            /* seconds from the back to the slot */
#define STAGGER 0.10f           /* between two letters' starts */
#define HOLD    2.6f
#define FLY_OUT 1.0f

/* one page: up to two lines; the font has A-Z, 0-9, space ! " # $ * , ? */
static const char *pages[][2] = {
    { "AMIX LIVES",       "AGAIN!" },
    { "AMIGA 3000",       "Z3660 68060" },
    { "COMMODORE UNIX",   "SVR4 1991" },
    { "TINYGL",           "ON X11R6,3" },
    { "NO BLITTER",       "WAS HARMED!" },
    { "GREETINGS TO",     "ALL AMIGA FREAKS" },
    { "LAMECUBE",         "AMIGA 42" },
};
#define NPAGES ((int)(sizeof pages / sizeof pages[0]))

/* the font strip: x position and width of each character from ' ' up
   (LamerIntroLinux's tables, as in LamerCubeAMIX) */
static const int startList[] = {622,651,661,676,702,0,0,0,0,0,952,0,968,0,0,0,
    723,747,759,783,805,830,854,878,903,928,0,0,0,0,0,985,0,0, 26,50,72,96,117,
    138,162,186,195,213,239,259,289,315,339,363,389,413,436,459,482,507,546,572,
    598};
static const int widthList[] = { 28, 7,12,23,19,0,0,0,0,0, 14,0, 7,0,0,0,
    22, 9,22,19,22,22,22,22,22,22,0,0,0,0,0,20, 0,25,23,21,23,20,20,23,23, 8,
    17,25,19,29,25,23,23,25,23,22,22,22,24,38,25,25,22};
#define NCHARS ((int)(sizeof widthList / sizeof widthList[0]))

/* a glyph as runs of blocks: row, first column, length */
#define ROWS    (FONT_H / 4)
#define MAXRUNS 40
struct run { signed char row, col, len; };
static struct run runs[NCHARS][MAXRUNS];
static int nruns[NCHARS], gcols[NCHARS];

/* the letters of the current page */
#define MAXLET  40
static struct letter {
    int g;                      /* glyph */
    float x, y;                 /* slot, centre of the glyph */
    float sx, sy;               /* where it starts, far back */
    float spin;                 /* degrees it turns on the way */
} let[MAXLET];
static int nlet, page;
static double pageStart;

static float starX[NSTARS], starY[NSTARS], starZ[NSTARS];
static GLuint texLogo;
static pid_t music = -1;
static int frames, runSeconds;
static double t0, last;

static double now(void)
{
    return glutGet(GLUT_ELAPSED_TIME) / 1000.0;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float) rand() / (float) RAND_MAX;
}

static int glyph(int c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    c -= ' ';
    if (c < 0 || c >= NCHARS || widthList[c] == 0)
        return 0;
    return c;
}

/* cut every glyph of the font strip into 4x4 cells, a cell set when at least
   6 of its 16 pixels are opaque, and merge each row's cells into runs (the
   strip is an OpenGL texture: its first row is the bottom one) */
static void makeGlyphs(void)
{
    int g, r, c, x, y, n;

    for (g = 1; g < NCHARS; g++) {
        if (widthList[g] == 0)
            continue;
        gcols[g] = (widthList[g] + 3) / 4;
        for (r = 0; r < ROWS; r++) {
            int start = -1;
            for (c = 0; c <= gcols[g]; c++) {
                int on = 0;
                if (c < gcols[g]) {
                    for (n = 0, y = r * 4; y < r * 4 + 4; y++)
                        for (x = startList[g] + c * 4; x < startList[g] + c * 4 + 4; x++)
                            if (x < startList[g] + widthList[g] &&
                                font_data[(y * FONT_W + x) * 4 + 3] > 127)
                                n++;
                    on = n >= 6;
                }
                if (on && start < 0)
                    start = c;
                if (!on && start >= 0) {
                    if (nruns[g] < MAXRUNS) {
                        runs[g][nruns[g]].row = ROWS - 1 - r;   /* rows are stored bottom up */
                        runs[g][nruns[g]].col = start;
                        runs[g][nruns[g]].len = c - start;
                        nruns[g]++;
                    }
                    start = -1;
                }
            }
        }
    }
    gcols[0] = 5;               /* a blank */
}

static float lineWidth(const char *s)
{
    float w = 0;
    for (; *s; s++)
        w += (gcols[glyph(*s)] + 1) * BLOCK;
    return w - BLOCK;
}

static void startPage(int p, double t)
{
    int l, i;
    float x, y;

    page = p % NPAGES;
    pageStart = t;
    nlet = 0;
    for (l = 0; l < 2; l++) {
        const char *s = pages[page][l];
        if (!s || !*s)
            continue;
        x = -lineWidth(s) / 2;
        y = l == 0 ? -1.9f : -3.5f;
        for (i = 0; s[i] && nlet < MAXLET; i++) {
            int g = glyph(s[i]);
            float w = gcols[g] * BLOCK;
            if (g) {
                let[nlet].g = g;
                let[nlet].x = x + w / 2;
                let[nlet].y = y;
                let[nlet].sx = (x + w / 2) * 4.0f + frand(-6, 6);
                let[nlet].sy = frand(-10, 14);
                let[nlet].spin = (nlet & 1 ? 1 : -1) * frand(300, 600);
                nlet++;
            }
            x += w + BLOCK;
        }
    }
}

/* Amiga-ish copper colours down a letter, row r of ROWS */
static void rowColour(int r, float bright)
{
    float u = (float) r / ROWS;
    glColor3f(bright * 1.0f, bright * (0.95f - 0.6f * u), bright * (0.25f + 0.5f * u * u));
}

/*
 * On soft3D (blending works there) every glyph is one textured quad: the
 * glyphs sit in a 256x256 RGBA atlas, 4x4 texels a cell with the row
 * colours baked in (the run quads' gradient, sampled per texel row), clear
 * where the cell is, and the brightness comes from the vertex colour
 * (MODULATE).  ~490 vertices a frame become ~100.  On the 68k TinyGL has
 * no alpha: the letters stay run quads there.
 */
#define TPC 4                           /* texels per cell */
static GLuint texAtlas;
static int useAtlas;
static float atlasS0[NCHARS], atlasT0[NCHARS], atlasS1[NCHARS], atlasT1[NCHARS];

static void makeAtlas(void)
{
    static unsigned char img[256 * 256 * 4];
    int g, i, x = 0, y = 0, shelf = ROWS * TPC + 2;

    memset(img, 0, sizeof img);
    for (g = 1; g < NCHARS; g++) {
        int w = gcols[g] * TPC + 2;
        if (!nruns[g])
            continue;
        if (x + w > 256) {
            x = 0;
            y += shelf;
        }
        if (y + shelf > 256)
            break;                      /* does not happen with this font */
        for (i = 0; i < nruns[g]; i++) {
            const struct run *r = &runs[g][i];
            int tx, ty;
            for (ty = 0; ty < TPC; ty++) {
                float u = (r->row + (ty + 0.5f) / TPC) / ROWS;
                unsigned char cr = 255, cg = (unsigned char) (255 * (0.95f - 0.6f * u)),
                              cb = (unsigned char) (255 * (0.25f + 0.5f * u * u));
                for (tx = 0; tx < r->len * TPC; tx++) {
                    unsigned char *p = &img[((y + 1 + r->row * TPC + ty) * 256 +
                                             x + 1 + r->col * TPC + tx) * 4];
                    p[0] = cr; p[1] = cg; p[2] = cb; p[3] = 255;
                }
            }
        }
        atlasS0[g] = (x + 1) / 256.0f;
        atlasS1[g] = (x + 1 + gcols[g] * TPC) / 256.0f;
        atlasT0[g] = (y + 1) / 256.0f;                  /* top row of the glyph */
        atlasT1[g] = (y + 1 + ROWS * TPC) / 256.0f;
        x += w;
    }
    glGenTextures(1, &texAtlas);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
}

static void drawLetter(const struct letter *L, float px, float py, float pz,
                       float angle, float bright)
{
    int i;

    if (useAtlas) {
        float w = gcols[L->g] * BLOCK, h = ROWS * BLOCK;
        glPushMatrix();
        glTranslatef(px, py, pz);
        if (angle != 0.0f)
            glRotatef(angle, 0.0f, 1.0f, 0.0f);
        glColor3f(bright, bright, bright);
        glBegin(GL_QUADS);
        glTexCoord2f(atlasS0[L->g], atlasT0[L->g]); glVertex3f(-w / 2,  h / 2, 0);
        glTexCoord2f(atlasS1[L->g], atlasT0[L->g]); glVertex3f( w / 2,  h / 2, 0);
        glTexCoord2f(atlasS1[L->g], atlasT1[L->g]); glVertex3f( w / 2, -h / 2, 0);
        glTexCoord2f(atlasS0[L->g], atlasT1[L->g]); glVertex3f(-w / 2, -h / 2, 0);
        glEnd();
        glPopMatrix();
        return;
    }
    float w = gcols[L->g] * BLOCK, h = ROWS * BLOCK;

    glPushMatrix();
    glTranslatef(px, py, pz);
    if (angle != 0.0f)
        glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    for (i = 0; i < nruns[L->g]; i++) {
        const struct run *r = &runs[L->g][i];
        float x0 = r->col * BLOCK - w / 2, x1 = x0 + r->len * BLOCK;
        float y0 = h / 2 - r->row * BLOCK, y1 = y0 - BLOCK;
        rowColour(r->row, bright);
        glVertex3f(x0, y0, 0); glVertex3f(x1, y0, 0);
        rowColour(r->row + 1, bright);
        glVertex3f(x1, y1, 0); glVertex3f(x0, y1, 0);
    }
    glEnd();
    glPopMatrix();
}

static void drawText(double t)
{
    double pt = t - pageStart, outAt = (nlet - 1) * STAGGER + FLY_IN + HOLD;
    int i;

    if (pt > outAt + FLY_OUT + (nlet - 1) * 0.03f + 0.3f) {
        startPage(page + 1, t);
        pt = 0;
        outAt = (nlet - 1) * STAGGER + FLY_IN + HOLD;
    }
    if (useAtlas) {
        /* cut-outs: no depth writes, or a letter's clear texels would hide
           the letters behind it (the cube still hides them: it is drawn first) */
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texAtlas);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        glDepthMask(GL_FALSE);
    }
    for (i = 0; i < nlet; i++) {
        const struct letter *L = &let[i];
        float u = (float) ((pt - i * STAGGER) / FLY_IN), o, e;
        if (u <= 0.0f)
            continue;
        o = (float) ((pt - outAt - i * 0.03f) / FLY_OUT);
        if (o >= 1.0f)
            continue;
        if (u > 1.0f)
            u = 1.0f;
        e = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);     /* ease out */
        if (o <= 0.0f) {
            /* in flight, then resting with a little wave */
            float wave = u >= 1.0f ? (float) sin(t * 3.0 + L->x * 0.7f) * 0.10f : 0.0f;
            drawLetter(L, L->sx + (L->x - L->sx) * e,
                       L->sy + (L->y - L->sy) * e + wave,
                       -90.0f + (ZTEXT + 90.0f) * e,
                       (1.0f - e) * L->spin, 0.35f + 0.65f * e);
        } else {
            /* out past the viewer, turning over */
            float a = o * o;
            drawLetter(L, L->x * (1.0f + 2.0f * a), L->y + (float) sin(t * 3.0 + L->x * 0.7f) * 0.10f,
                       ZTEXT + (ZTEXT * -1.0f + 1.5f) * a, a * 200.0f, 1.0f);
        }
    }
    if (useAtlas) {
        glDepthMask(GL_TRUE);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_TEXTURE_2D);
    }
}

static void newStar(int i, int far)
{
    starX[i] = frand(-40, 40);
    starY[i] = frand(-25, 25);
    starZ[i] = far ? -110.0f : frand(-110.0f, -2.0f);
}

static void drawStars(float dt)
{
    int i;

    glBegin(GL_POINTS);
    for (i = 0; i < NSTARS; i++) {
        float b;
        starZ[i] += dt * 22.0f;
        if (starZ[i] > -2.0f)
            newStar(i, 1);
        b = 1.0f + starZ[i] / 110.0f;
        glColor3f(b, b, b);
        glVertex3f(starX[i], starY[i], starZ[i]);
    }
    glEnd();
}

static void drawCube(float t)
{
    static const float v[6][4][3] = {
        {{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}},
        {{ 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}},
        {{ 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}},
        {{-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}},
        {{-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1}},
        {{-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1}}
    };
    /* half a texel in from the edges: TinyGL wraps 1.0 onto the far side */
#define T0 (0.5f / 256)
#define T1 (1.0f - 0.5f / 256)
    static const float tc[4][2] = { {T0, T0}, {T1, T0}, {T1, T1}, {T0, T1} };
    int f, k;

    if (getenv("LAMECUBE_STILL"))     /* test: the front face, upright */
        t = 0.0f;
    glPushMatrix();
    glTranslatef(0.0f, 1.25f, -8.0f + (float) sin(t * 0.8f) * 0.8f);
    glRotatef(t * 50.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(t * 70.0f, 0.0f, 1.0f, 0.0f);
    glRotatef(t * 30.0f, 0.0f, 0.0f, 1.0f);
    if (getenv("LAMECUBE_FACE")) {      /* test: turn face n to the viewer */
        static const float r[6][4] = { {0,0,1,0}, {180,0,1,0}, {-90,0,1,0},
                                       {90,0,1,0}, {90,1,0,0}, {-90,1,0,0} };
        const float *q = r[atoi(getenv("LAMECUBE_FACE")) % 6];
        glRotatef(q[0], q[1], q[2], q[3]);
    }
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texLogo);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    for (f = 0; f < 6; f++)
        for (k = 0; k < 4; k++) {
            glTexCoord2f(tc[k][0], tc[k][1]);
            glVertex3f(v[f][k][0], v[f][k][1], v[f][k][2]);
        }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glPopMatrix();
}

static void startMusic(const char *module)
{
    if (access(module, R_OK) != 0) {
        fprintf(stderr, "lamecubeamiga42: no module %s, no music\n", module);
        return;
    }
    music = fork();
    if (music == 0) {
        execlp("mikmod", "mikmod", "-q", module, (char *) 0);
        _exit(1);
    }
}

static void quit(void)
{
    double t = now() - t0;

    if (music > 0) {
        kill(music, SIGTERM);
        waitpid(music, (int *) 0, 0);
    }
    if (t > 0)
        fprintf(stderr, "lamecubeamiga42: %.1f fps (%d frames in %.1f s)\n", frames / t, frames, t);
    exit(0);
}

/*
 * Xzz9000 copies the swap with move16 when the window's root x is a multiple
 * of 8.  One test move shows where the window manager puts the window for a
 * requested position (frame, title); the second request lands it aligned.
 */
static void alignWindow(int frame)
{
    static int x0, y0;
    int x, y;

    if (frame != 3 && frame != 10)
        return;
    x = glutGet(GLUT_WINDOW_X);
    y = glutGet(GLUT_WINDOW_Y);
    if (frame == 3) {
        x0 = x;
        y0 = y;
        if (x % 8 != 0)
            glutPositionWindow(x0, y0);
    } else if (x0 % 8 != 0) {
        glutPositionWindow(x0 - (x - x0) - x0 % 8, y0 - (y - y0));
    }
}

static void display(void)
{
    double t = now();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    drawStars((float) (t - last));
    drawCube((float) (t - t0));
    drawText(t - t0);
    glutSwapBuffers();
    last = t;
    alignWindow(++frames);
    if (runSeconds > 0 && t - t0 >= runSeconds)
        quit();
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
    int i;

    glutInit(&argc, argv);
    if (argc > 1)
        module = argv[1];
    if (getenv("LAMECUBE_SECONDS"))
        runSeconds = atoi(getenv("LAMECUBE_SECONDS"));
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WIN_W, WIN_H);
    glutInitWindowPosition(64, 64);
    glutCreateWindow("LameCubeAMIGA42");

    makeGlyphs();
    if (glAmixSoft3D() && !getenv("LAMECUBE_RUNS")) {
        useAtlas = 1;
        makeAtlas();
    }
    glGenTextures(1, &texLogo);
    glBindTexture(GL_TEXTURE_2D, texLogo);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, LOGO_W, LOGO_H, 0, GL_RGB, GL_UNSIGNED_BYTE, logo_data);
    for (i = 0; i < NSTARS; i++)
        newStar(i, 0);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.8, 0.8, -0.5, 0.5, 1.0, 120.0);
    glMatrixMode(GL_MODELVIEW);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutKeyboardFunc(key);
    startMusic(module);
    t0 = last = now();
    startPage(0, 0.0);
    glutMainLoop();
    return 0;
}
