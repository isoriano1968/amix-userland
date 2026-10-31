/*
 * lamerintro.c -- LamerIntro for AMIX: the party invitation for the Classic
 * Computing 2022, from the Linux and IRIX version, with the GL on the Z3660's
 * core 1 (zzgl).  Starfield, rasters, the bitmap scroller, the sine logo and
 * the tunnel as there, and the 3D scroller the original never got to draw: its
 * letters are the font's pixels as lit boxes, in front of the tunnel.
 *
 *   lamerintro [module]     ESC or q quits
 *
 * The textures are read from the working directory; libmikmod mixes the
 * module here and the sound goes to the board's HDMI through zzgl's stream.
 * LAMER_START=n starts at second n, LAMER_SECONDS=n stops after n seconds and
 * prints the frame rate.  LAMER_W, LAMER_H: the window, 640x480 by default.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include "zzgl_enc.h"
#include "zzgl_platform.h"
#include <mikmod.h>
#include "letters3d.h"

#define PI 3.1415927f

#define MAXNUMBERRASTERS 50
#define TRANSITIONTIME 2.0f
#define NB_STARS 250

#define STAR_XMIN -35
#define STAR_XMAX 35
#define STAR_YMIN -25
#define STAR_YMAX 15
#define STAR_ZMIN -50
#define STAR_ZMAX 0
#define STAR_V 15

#define TUNNEL_RADIUS 6
#define TUNNEL_MINZ -10
#define TUNNEL_MAXZ 60
#define TUNNEL_CIRCUMPIECES 25
#define TUNNEL_DEPTH_PIECES 25

#define SINELOGO_SUBTILES 16

#define BITMAP_SCROLLER_LETTER_SPACING 15
#define NBBITMAPSCROLLINGLETTERS 40
#define NB3DLETTERS 8
#define LETTER3D_SPACING 2.2f
#define LETTER3D_DEPTH 0.3f

#define PHRASE1 "*** LOOKS LIKE THE LAMER INTRO IS EVEN RUNNING ON AMIX *** REMEMBER *** ONLY AMIGA MAKES IT POSSIBLE "
#define PHRASE2 "*** MISSING HP-UX AND AIX "
#define PHRASE3 "*** AND BTW 2022 WE HAVE THE CLASSIC COMPUTING MEETING IN LINGEN/GERMANY FROM 8TH TILL 11TH OF SEPTEMBER "

/* the GL, by name from zzgl */
#define GL_FUNCS(X) \
   X(void, Begin, (GLenum)) X(void, End, (void)) X(void, Vertex2f, (GLfloat, GLfloat)) \
   X(void, Vertex3f, (GLfloat, GLfloat, GLfloat)) X(void, TexCoord2f, (GLfloat, GLfloat)) \
   X(void, Normal3f, (GLfloat, GLfloat, GLfloat)) X(void, Color3f, (GLfloat, GLfloat, GLfloat)) \
   X(void, Color4f, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, Enable, (GLenum)) \
   X(void, Disable, (GLenum)) X(void, BlendFunc, (GLenum, GLenum)) \
   X(void, BindTexture, (GLenum, GLuint)) X(void, GenTextures, (GLsizei, GLuint *)) \
   X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
   X(void, TexParameteri, (GLenum, GLenum, GLint)) X(void, Clear, (GLbitfield)) \
   X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, MatrixMode, (GLenum)) \
   X(void, LoadIdentity, (void)) X(void, PushMatrix, (void)) X(void, PopMatrix, (void)) \
   X(void, Translatef, (GLfloat, GLfloat, GLfloat)) X(void, Rotatef, (GLfloat, GLfloat, GLfloat, GLfloat)) \
   X(void, Frustum, (GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)) \
   X(void, Ortho, (GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)) \
   X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) X(void, ShadeModel, (GLenum)) \
   X(void, Lightfv, (GLenum, GLenum, const GLfloat *))

#define X(ret, name, args) static ret (*gl_##name) args;
GL_FUNCS(X)
#undef X

static int W = 640, H = 480;
static Display *dpy;
static Window win;
static int win_x, win_y;
static MODULE *module;



enum { T_RASTER_PLAIN, T_RASTER, T_BACKGROUND, T_FONT, T_SINELOGO, T_TROMBINE, T_STAR, T_LOGO, T_TITLE, NB_TEXTURES };
static const char *const tex_file[NB_TEXTURES] = {
   "raster_plain.tga", "raster.tga", "background.tga", "fonte.tga", "sinelogo.tga",
   "trombine.tga", "star.tga", "sgi.tga", "title.tga"
};
static GLuint tex[NB_TEXTURES];

/* where each letter sits in fonte.tga: Sp ! " # $ % & ' ( ) * + , - . / 0..9 : ; < = > ? @ A..Z */
static const short startList[] = {
   622, 651, 661, 676, 702, 0, 0, 0, 0, 0, 952, 0, 968, 0, 0, 0, 723, 747, 759, 783, 805, 830, 854, 878, 903, 928, 0,
   0, 0, 0, 0, 985, 0, 0, 26, 50, 72, 96, 117, 138, 162, 186, 195, 213, 239, 259, 289, 315, 339, 363, 389, 413, 436,
   459, 482, 507, 546, 572, 598
};
static const short widthList[] = {
   28, 7, 12, 23, 19, 0, 0, 0, 0, 0, 14, 0, 7, 0, 0, 0, 22, 9, 22, 19, 22, 22, 22, 22, 22, 22, 0, 0, 0, 0, 0, 20, 0,
   25, 23, 21, 23, 20, 20, 23, 23, 8, 17, 25, 19, 29, 25, 23, 23, 25, 23, 22, 22, 22, 24, 38, 25, 25, 22
};

/* the effects, as the original's phases */
static int showRasters, showStarfield, showTunnel, showSineLogo, showBackground, bitmapScroller, showDemoLogo,
           showHeaderLogo, rasterBlend = 1, verticalRaster, scroller3d;
static int nbRasters = MAXNUMBERRASTERS / 3, rasterTex = T_RASTER_PLAIN, backgroundTex = T_BACKGROUND;
static int phase, effectDone[7];
static float demotimer, dt, cnt1;
static int transition, flashDir = 1;
static float flash, virtualFlash;
static float backgroundAngle, backgroundTile, texShiftX, texShiftY;
static float scrollerX = 1.2f;
static const float Zdistance = -6.0f;
static char scrollerText[512];

static struct { float rotate, thickness, width, x, y, r, g, b; } rasters[MAXNUMBERRASTERS];
static struct { float x, y, z; } stars[NB_STARS];
static struct { float x, y; } sineTiles[SINELOGO_SUBTILES + 1][SINELOGO_SUBTILES + 1];
static struct { float x, y; int letter; } bitmapLetters[NBBITMAPSCROLLINGLETTERS];
static struct { float y; int letter; } letters3d[NB3DLETTERS];

static float
now(void)
{
   static struct timeval t0;
   struct timeval t;

   gettimeofday(&t, NULL);
   if (!t0.tv_sec)
      t0 = t;
   return (float)(t.tv_sec - t0.tv_sec) + (float)(t.tv_usec - t0.tv_usec) * 1e-6f;
}

static void
stop_music(void)
{
   if (module) {
      Player_Stop();
      MikMod_Exit();
   }
   module = NULL;
}

static void
quit(int code)
{
   stop_music();
   zzgl_platform_close();
   exit(code);
}

static void
on_signal(int s)
{
   (void)s;
   quit(1);
}

static void
start_music(const char *file)
{
   if (access(file, R_OK) < 0)
      return;
   MikMod_RegisterDriver(&drv_hdmi);
   MikMod_RegisterAllLoaders();
   md_device = MikMod_DriverFromAlias((CHAR *)"hdmi");
   if (MikMod_Init("")) {
      fprintf(stderr, "lamerintro: %s\n", MikMod_strerror(MikMod_errno));
      return;
   }
   module = Player_Load((CHAR *)file, 32, 0);
   if (!module) {
      fprintf(stderr, "lamerintro: %s: %s\n", file, MikMod_strerror(MikMod_errno));
      return;
   }
   module->wrap = 1;
   Player_Start(module);
}

/* TGA, uncompressed, its rows as stored, as the original read them */
static void
load_texture(int n)
{
   unsigned char head[18];
   unsigned char *d;
   unsigned w, h, bytes, size, i;
   FILE *f = fopen(tex_file[n], "rb");

   if (!f || fread(head, 1, sizeof(head), f) != sizeof(head)) {
      fprintf(stderr, "lamerintro: no %s\n", tex_file[n]);
      exit(1);
   }
   w = head[13] * 256 + head[12];
   h = head[15] * 256 + head[14];
   bytes = head[16] / 8;
   size = w * h * bytes;
   fseek(f, head[0], SEEK_CUR);
   d = malloc(size);
   if (!d || fread(d, 1, size, f) != size) {
      fprintf(stderr, "lamerintro: short %s\n", tex_file[n]);
      exit(1);
   }
   fclose(f);
   for (i = 0; i < size; i += bytes) {
      const unsigned char t = d[i];

      d[i] = d[i + 2];
      d[i + 2] = t;
   }
   gl_GenTextures(1, &tex[n]);
   gl_BindTexture(GL_TEXTURE_2D, tex[n]);
   gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   gl_TexImage2D(GL_TEXTURE_2D, 0, bytes == 4 ? GL_RGBA : GL_RGB, w, h, 0, bytes == 4 ? GL_RGBA : GL_RGB,
                 GL_UNSIGNED_BYTE, d);
   free(d);
}

static void
perspective(void)
{
   const double top = 0.1 * tan(60.0 * 3.14159265 / 360.0), right = top * W / H;

   gl_MatrixMode(GL_PROJECTION);
   gl_LoadIdentity();
   gl_Frustum(-right, right, -top, top, 0.1, 100.0);
   gl_MatrixMode(GL_MODELVIEW);
}

static void
ortho(void)
{
   gl_MatrixMode(GL_PROJECTION);
   gl_LoadIdentity();
   gl_Ortho(0, W, 0, H, -100, 100);
   gl_MatrixMode(GL_MODELVIEW);
   gl_LoadIdentity();
}

static void
init_gl(void)
{
   static const GLfloat diffuse[] = { 0.8f, 0.8f, 0.8f, 1.0f }, pos[] = { 0.0f, 0.0f, 3.0f, 1.0f },
                        ambient[] = { 0.25f, 0.25f, 0.25f, 1.0f };
   int i;

   gl_ClearColor(0, 0, 0, 0);
   gl_ShadeModel(GL_SMOOTH);
   gl_Lightfv(GL_LIGHT0, GL_AMBIENT, ambient);
   gl_Lightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
   gl_Lightfv(GL_LIGHT0, GL_POSITION, pos);
   gl_Enable(GL_LIGHT0);
   gl_Enable(GL_COLOR_MATERIAL);
   gl_Viewport(0, 0, W, H);
   for (i = 0; i < NB_TEXTURES; i++)
      load_texture(i);
   for (i = 0; i < NB_STARS; i++) {
      stars[i].x = STAR_XMIN + (float)rand() * (STAR_XMAX - STAR_XMIN) / RAND_MAX;
      stars[i].y = STAR_YMIN + (float)rand() * (STAR_YMAX - STAR_YMIN) / RAND_MAX;
      stars[i].z = STAR_ZMIN + (float)rand() * (STAR_ZMAX - STAR_ZMIN) / RAND_MAX;
   }
}

static void
rotate2(float x, float y, float *ox, float *oy, float a)
{
   *ox = x * cosf(a) + y * sinf(a);
   *oy = x * sinf(a) - y * cosf(a);
}

static void
draw_stars(void)
{
   int i;

   perspective();
   gl_LoadIdentity();
   gl_Translatef(0, 0, Zdistance);
   gl_BindTexture(GL_TEXTURE_2D, tex[T_STAR]);
   gl_Begin(GL_QUADS);
   for (i = 0; i < NB_STARS; i++) {
      const float fade = 0.75f * (stars[i].z - STAR_ZMIN) / (STAR_ZMAX - STAR_ZMIN);

      gl_Color3f(fade, fade, fade);
      gl_TexCoord2f(0, 0);
      gl_Vertex3f(stars[i].x, stars[i].y, stars[i].z);
      gl_TexCoord2f(1, 0);
      gl_Vertex3f(stars[i].x + 1, stars[i].y, stars[i].z);
      gl_TexCoord2f(1, 1);
      gl_Vertex3f(stars[i].x + 1, stars[i].y + 1, stars[i].z);
      gl_TexCoord2f(0, 1);
      gl_Vertex3f(stars[i].x, stars[i].y + 1, stars[i].z);
   }
   gl_End();
}

static void
draw_background(void)
{
   float x1, y1, x2, y2, x3, y3, x4, y4;

   rotate2(-backgroundTile / 2, -backgroundTile / 2, &x1, &y1, backgroundAngle);
   rotate2(backgroundTile / 2, -backgroundTile / 2, &x2, &y2, backgroundAngle);
   rotate2(backgroundTile / 2, backgroundTile / 2, &x3, &y3, backgroundAngle);
   rotate2(-backgroundTile / 2, backgroundTile / 2, &x4, &y4, backgroundAngle);
   gl_BindTexture(GL_TEXTURE_2D, tex[backgroundTex]);
   if (rasterBlend)
      gl_Enable(GL_BLEND);
   else
      gl_Disable(GL_BLEND);
   gl_Color4f(0.65f, 0.65f, 0.65f, 0.6f);
   gl_Begin(GL_QUADS);
   gl_TexCoord2f(texShiftX + x1, texShiftY + y1);
   gl_Vertex2f(0, 0);
   gl_TexCoord2f(texShiftX + x2, texShiftY + y2);
   gl_Vertex2f(W, 0);
   gl_TexCoord2f(texShiftX + x3, texShiftY + y3);
   gl_Vertex2f(W, H);
   gl_TexCoord2f(texShiftX + x4, texShiftY + y4);
   gl_Vertex2f(0, H);
   gl_End();
}

static void
draw_sinelogo(void)
{
   const float n = SINELOGO_SUBTILES;
   int i, j;

   gl_BindTexture(GL_TEXTURE_2D, tex[T_SINELOGO]);
   gl_Color4f(1, 1, 1, 1);
   gl_Begin(GL_QUADS);
   for (i = 0; i < SINELOGO_SUBTILES; i++)
      for (j = 0; j < SINELOGO_SUBTILES; j++) {
         gl_TexCoord2f(i / n, -j / n);
         gl_Vertex2f(sineTiles[i][j].x, sineTiles[i][j].y);
         gl_TexCoord2f((i + 1) / n, -j / n);
         gl_Vertex2f(sineTiles[i + 1][j].x, sineTiles[i + 1][j].y);
         gl_TexCoord2f((i + 1) / n, -(j + 1) / n);
         gl_Vertex2f(sineTiles[i + 1][j + 1].x, sineTiles[i + 1][j + 1].y);
         gl_TexCoord2f(i / n, -(j + 1) / n);
         gl_Vertex2f(sineTiles[i][j + 1].x, sineTiles[i][j + 1].y);
      }
   gl_End();
}

/* the logo hops to the music, on these rows of the module's middle part */
static void
draw_demologo(void)
{
   const int song = module ? module->sngpos : 0, row = module ? module->patpos : 0;
   const int hop = ((song >= 8 && song <= 15) || (song >= 18 && song <= 20)) &&
                   (row == 13 || row == 32 || row == 48 || row == 56 || row == 60 || row == 62);
   const float x = W - 70, y = hop ? 30 : 5;

   gl_BindTexture(GL_TEXTURE_2D, tex[T_LOGO]);
   gl_Color4f(0, 0, 1, 0.7f);
   gl_Begin(GL_QUADS);
   gl_TexCoord2f(0, 0);
   gl_Vertex2f(x, y);
   gl_TexCoord2f(0, 1);
   gl_Vertex2f(x, y + 50);
   gl_TexCoord2f(1, 1);
   gl_Vertex2f(x + 64, y + 50);
   gl_TexCoord2f(1, 0);
   gl_Vertex2f(x + 64, y);
   gl_End();
}

static void
draw_headerlogo(void)
{
   const float sx = 0.8f + 0.3f * cosf(cnt1 / 5), w = 512 * sx, x = W / 2 - w / 2, y = H - 74;

   gl_BindTexture(GL_TEXTURE_2D, tex[T_TITLE]);
   gl_Color4f(0.5f, 0.5f, 0.5f, 1);
   gl_Begin(GL_QUADS);
   gl_TexCoord2f(0, 0);
   gl_Vertex2f(x, y);
   gl_TexCoord2f(1, 0);
   gl_Vertex2f(x + w, y);
   gl_TexCoord2f(1, 1);
   gl_Vertex2f(x + w, y + 64);
   gl_TexCoord2f(0, 1);
   gl_Vertex2f(x, y + 64);
   gl_End();
   cnt1 += 12.5f * dt;
}

static void
raster_vertex(int i, float x, float y)
{
   const float a = rasters[i].rotate * PI / 180;

   gl_Vertex2f(x * cosf(a) - y * sinf(a), x * sinf(a) + y * cosf(a) + rasters[i].y);
}

static void
draw_rasters(void)
{
   int i;

   gl_BindTexture(GL_TEXTURE_2D, tex[rasterTex]);
   if (rasterBlend)
      gl_Enable(GL_BLEND);
   else
      gl_Disable(GL_BLEND);
   gl_Begin(GL_QUADS);
   for (i = 0; i < nbRasters; i++) {
      const float x0 = rasters[i].x - 0.5f * rasters[i].width, x1 = rasters[i].x + 0.5f * rasters[i].width;
      const float y0 = -0.5f * rasters[i].thickness, y1 = 0.5f * rasters[i].thickness;

      gl_Color4f(rasters[i].r, rasters[i].g, rasters[i].b, 1);
      if (!verticalRaster) {
         gl_TexCoord2f(0, 0);
         raster_vertex(i, x0, y0);
         gl_TexCoord2f(16, 0);
         raster_vertex(i, x1, y0);
         gl_TexCoord2f(16, 1);
         raster_vertex(i, x1, y1);
         gl_TexCoord2f(0, 1);
         raster_vertex(i, x0, y1);
      } else {
         gl_TexCoord2f(16, 0);
         raster_vertex(i, x0, y0);
         gl_TexCoord2f(16, 1);
         raster_vertex(i, x1, y0);
         gl_TexCoord2f(0, 1);
         raster_vertex(i, x1, y1);
         gl_TexCoord2f(0, 0);
         raster_vertex(i, x0, y1);
      }
   }
   gl_End();
}

static void
draw_bitmap_scroller(void)
{
   int i;

   gl_BindTexture(GL_TEXTURE_2D, tex[T_FONT]);
   gl_Color4f(1, 1, 1, 1);
   gl_Begin(GL_QUADS);
   for (i = 0; i < NBBITMAPSCROLLINGLETTERS; i++) {
      const int c = scrollerText[bitmapLetters[i].letter] - 32, width = widthList[c];
      const float s0 = 1.001f * startList[c] / 1024, s1 = 0.999f * (startList[c] + width) / 1024;
      const float x = (float)(int)bitmapLetters[i].x, y = bitmapLetters[i].y;

      gl_TexCoord2f(s0, 0.99f);
      gl_Vertex2f(x, y);
      gl_TexCoord2f(s0, 0.01f);
      gl_Vertex2f(x, y + 40);
      gl_TexCoord2f(s1, 0.01f);
      gl_Vertex2f(x + width, y + 40);
      gl_TexCoord2f(s1, 0.99f);
      gl_Vertex2f(x + width, y);
   }
   gl_End();
}

static void
draw_tunnel(void)
{
   int z, k;

   perspective();
   gl_LoadIdentity();
   gl_Enable(GL_LIGHTING);
   gl_ShadeModel(GL_FLAT);
   gl_Disable(GL_BLEND);
   gl_Disable(GL_TEXTURE_2D);
   gl_Translatef(0, 0, Zdistance);
   gl_Rotatef(30, 1, 0, 0);
   gl_Rotatef(42 + 80, 0, 1, 0);
   gl_Begin(GL_QUADS);
   for (z = 0; z < TUNNEL_DEPTH_PIECES; z++) {
      const float rad0 = TUNNEL_RADIUS * (2.5f + 0.5f * cosf(2 * demotimer + 0.5f * z));
      const float rad1 = TUNNEL_RADIUS * (2.5f + 0.5f * cosf(2 * demotimer + 0.5f * (z + 1)));
      const float z0 = TUNNEL_MINZ + (float)z * (TUNNEL_MAXZ - TUNNEL_MINZ) / TUNNEL_DEPTH_PIECES;
      const float z1 = TUNNEL_MINZ + (float)(z + 1) * (TUNNEL_MAXZ - TUNNEL_MINZ) / TUNNEL_DEPTH_PIECES;
      const float ox = 3 * cosf(demotimer) + 4, oy = 3 * sinf(demotimer) + 6;

      for (k = TUNNEL_CIRCUMPIECES / 2; k < TUNNEL_CIRCUMPIECES; k++) {
         const float a0 = 2 * PI * k / TUNNEL_CIRCUMPIECES, a1 = 2 * PI * (k + 1) / TUNNEL_CIRCUMPIECES;

         gl_Color3f(0.2f + 0.8f * sinf(PI * z / TUNNEL_DEPTH_PIECES) * cosf(0.3f * demotimer),
                    0.3f + 0.7f * sinf(PI * k / TUNNEL_CIRCUMPIECES) * sinf(0.1f * demotimer),
                    0.1f + 0.6f * sinf(0.5f * demotimer) * sinf(0.5f * demotimer));
         gl_Normal3f(-cosf(a0), -sinf(a0), 0);
         gl_Vertex3f(rad0 * cosf(a0) + ox, rad0 * sinf(a0) + oy, z0);
         gl_Vertex3f(rad1 * cosf(a0) + ox, rad1 * sinf(a0) + oy, z1);
         gl_Vertex3f(rad1 * cosf(a1) + ox, rad1 * sinf(a1) + oy, z1);
         gl_Vertex3f(rad0 * cosf(a1) + ox, rad0 * sinf(a1) + oy, z0);
      }
   }
   gl_End();
   gl_Disable(GL_LIGHTING);
   gl_ShadeModel(GL_SMOOTH);
}

/* one letter: its boxes' front, top, bottom and sides (the back never shows) */
static void
draw_letter(int c)
{
   const float cell = 1.9f / LETTER_CELLS, d = LETTER3D_DEPTH;
   int i;

   if (c < 32 || c > 90)
      return;
   c -= 32;
   for (i = letter_first[c]; i < letter_first[c + 1]; i++) {
      const float x0 = letter_box[i][0] * cell, y0 = letter_box[i][1] * cell;
      const float x1 = letter_box[i][2] * cell, y1 = letter_box[i][3] * cell;

      gl_Normal3f(0, 0, 1);
      gl_Vertex3f(x0, y0, d);
      gl_Vertex3f(x1, y0, d);
      gl_Vertex3f(x1, y1, d);
      gl_Vertex3f(x0, y1, d);
      gl_Normal3f(0, 1, 0);
      gl_Vertex3f(x0, y1, d);
      gl_Vertex3f(x1, y1, d);
      gl_Vertex3f(x1, y1, -d);
      gl_Vertex3f(x0, y1, -d);
      gl_Normal3f(0, -1, 0);
      gl_Vertex3f(x0, y0, -d);
      gl_Vertex3f(x1, y0, -d);
      gl_Vertex3f(x1, y0, d);
      gl_Vertex3f(x0, y0, d);
      gl_Normal3f(1, 0, 0);
      gl_Vertex3f(x1, y0, d);
      gl_Vertex3f(x1, y0, -d);
      gl_Vertex3f(x1, y1, -d);
      gl_Vertex3f(x1, y1, d);
      gl_Normal3f(-1, 0, 0);
      gl_Vertex3f(x0, y0, -d);
      gl_Vertex3f(x0, y0, d);
      gl_Vertex3f(x0, y1, d);
      gl_Vertex3f(x0, y1, -d);
   }
}

/* The last effect, in front of the tunnel: the letters rise on a sine, each
   swinging about its own axis. */
static void
draw_scroller3d(void)
{
   int k;

   perspective();
   gl_LoadIdentity();
   gl_Enable(GL_LIGHTING);
   gl_Enable(GL_DEPTH_TEST);
   gl_Enable(GL_CULL_FACE);
   gl_Disable(GL_TEXTURE_2D);
   gl_Disable(GL_BLEND);
   gl_Clear(GL_DEPTH_BUFFER_BIT);
   gl_Translatef(scrollerX, 0, Zdistance);
   for (k = 0; k < NB3DLETTERS; k++) {
      const float y = letters3d[k].y, w = 2 * demotimer + 0.45f * y;
      const int c = scrollerText[letters3d[k].letter];

      if (c == ' ')
         continue;
      gl_PushMatrix();
      gl_Translatef(sinf(w), y, 0.5f * sinf(demotimer + 0.8f * y));
      gl_Rotatef(45 * sinf(w), 0, 1, 0);
      gl_Color3f(1, 0.15f + 0.1f * sinf(w), 0);
      gl_Begin(GL_QUADS);
      draw_letter(c);
      gl_End();
      gl_PopMatrix();
   }
   gl_Disable(GL_CULL_FACE);
   gl_Disable(GL_DEPTH_TEST);
   gl_Disable(GL_LIGHTING);
}

static void
draw_flash(void)
{
   ortho();
   gl_Disable(GL_TEXTURE_2D);
   gl_Enable(GL_BLEND);
   gl_Color4f(1, 1, 1, flash);
   gl_Begin(GL_QUADS);
   gl_Vertex2f(0, 0);
   gl_Vertex2f(W, 0);
   gl_Vertex2f(W, H);
   gl_Vertex2f(0, H);
   gl_End();
}

static void
draw_scene(void)
{
   gl_Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   gl_Enable(GL_BLEND);
   gl_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   gl_Enable(GL_TEXTURE_2D);
   if (showStarfield)
      draw_stars();
   ortho();
   if (showBackground)
      draw_background();
   gl_Enable(GL_BLEND);
   if (showSineLogo)
      draw_sinelogo();
   if (showDemoLogo)
      draw_demologo();
   if (showHeaderLogo)
      draw_headerlogo();
   if (showRasters)
      draw_rasters();
   gl_Enable(GL_BLEND);
   if (bitmapScroller)
      draw_bitmap_scroller();
   if (showTunnel)
      draw_tunnel();
   if (scroller3d)
      draw_scroller3d();
   if (transition)
      draw_flash();
   zzgl_swap();
   zzgl_platform_frame();
}

static void
effect(int n)
{
   int i, total = 0;

   switch (n) {
   case 0:
      showRasters = showStarfield = showDemoLogo = showHeaderLogo = 1;
      rasterTex = T_RASTER_PLAIN;
      backgroundTex = T_BACKGROUND;
      backgroundTile = 8;
      strcpy(scrollerText, PHRASE1);
      phase = 2;
      break;
   case 1:
      strcpy(scrollerText, PHRASE1);
      strcat(scrollerText, PHRASE2);
      strcat(scrollerText, PHRASE3);
      for (i = 0; i < NBBITMAPSCROLLINGLETTERS; i++) {
         bitmapLetters[i].x = W + 100 + total;
         bitmapLetters[i].y = 0;
         bitmapLetters[i].letter = i;
         total += widthList[scrollerText[i] - 32] + BITMAP_SCROLLER_LETTER_SPACING;
      }
      rasterTex = T_RASTER;
      nbRasters = MAXNUMBERRASTERS / 3;
      showRasters = showBackground = showDemoLogo = bitmapScroller = showHeaderLogo = 1;
      showStarfield = 0;
      phase = 3;
      break;
   case 2:
      showRasters = showStarfield = showDemoLogo = 1;
      showBackground = bitmapScroller = 0;
      nbRasters = MAXNUMBERRASTERS / 2;
      phase = 4;
      break;
   case 3:
      showRasters = showBackground = showDemoLogo = 1;
      showStarfield = 0;
      nbRasters = MAXNUMBERRASTERS;
      phase = 5;
      break;
   case 4:
      backgroundTex = T_LOGO;
      verticalRaster = 1;
      showRasters = showBackground = showSineLogo = 1;
      phase = 6;
      break;
   case 5:
      backgroundTex = T_TROMBINE;
      showRasters = 0;
      showBackground = showSineLogo = 1;
      verticalRaster = 0;
      phase = 7;
      break;
   case 6:
      showBackground = showSineLogo = showDemoLogo = showHeaderLogo = 0;
      showTunnel = scroller3d = 1;
      strcpy(scrollerText, PHRASE1);
      strcat(scrollerText, PHRASE2);
      strcat(scrollerText, PHRASE3);
      for (i = 0; i < NB3DLETTERS; i++) {
         letters3d[i].y = -4.5f - LETTER3D_SPACING * i;
         letters3d[i].letter = i;
      }
      phase = 8;
      break;
   }
   effectDone[n] = 1;
}

static void
raster_colours(int i)
{
   rasters[i].r = cosf(3 * demotimer) + (1 - cosf(3 * demotimer)) * (float)i / nbRasters;
   rasters[i].g = 1 - (float)i / nbRasters;
   rasters[i].b = sinf(2 * demotimer) + (1 - sinf(2 * demotimer)) * (float)i / nbRasters;
}

static void
update(void)
{
   static const int runtime[7] = { 0, 30, 40, 50, 60, 70, 80 };
   const float step = dt * 50;          /* the original moved by 1/50 s a frame */
   int i, j;

   for (i = 6; i >= 0; i--) {
      if (effectDone[i])
         break;
      if (demotimer > runtime[i]) {
         for (j = 0; j <= i; j++)
            if (!effectDone[j])
               effect(j);
         break;
      }
      if (demotimer > runtime[i] - TRANSITIONTIME / 2 && (i == 0 || effectDone[i - 1]))
         transition = 1;
   }
   if (transition) {
      virtualFlash += flashDir * 2.4f * dt / TRANSITIONTIME;
      if (virtualFlash > 1.2f) {
         flash = 1;
         flashDir = -1;
      } else if (virtualFlash > 1) {
         flash = 1;
      } else if (virtualFlash < 0) {
         virtualFlash = flash = 0;
         flashDir = 1;
         transition = 0;
      } else {
         flash = virtualFlash;
      }
   }

   switch (phase) {
   case 2:
      for (i = 0; i < NB_STARS; i++) {
         stars[i].x += sinf(0.6f * demotimer) * STAR_V * dt;
         if (stars[i].x > STAR_XMAX)
            stars[i].x = STAR_XMIN;
         if (stars[i].x < STAR_XMIN)
            stars[i].x = STAR_XMAX;
         stars[i].z += 1.5f * STAR_V * dt;
         if (stars[i].z > STAR_ZMAX)
            stars[i].z = STAR_ZMIN;
         stars[i].y += cosf(0.5f * demotimer) * STAR_V * dt;
         if (stars[i].y > STAR_YMAX)
            stars[i].y = STAR_YMIN;
         if (stars[i].y < STAR_YMIN)
            stars[i].y = STAR_YMAX;
      }
      for (i = 0; i < nbRasters; i++) {
         rasters[i].thickness = 32;
         rasters[i].width = W;
         rasters[i].rotate = 0;
         rasters[i].y = (H - 128) / 2 + (H - 128) / 2 * sinf(2 * (demotimer + 0.05f * i));
         rasters[i].x = W / 2;
         rasters[i].r = rasters[i].g = 0;
         rasters[i].b = 0.5f + 0.5f * (float)i / nbRasters;
      }
      break;
   case 3:
      backgroundAngle += 0.05f * 0.01f * step;
      for (i = 0; i < NBBITMAPSCROLLINGLETTERS; i++) {
         bitmapLetters[i].x -= 300 * 0.009f * step;
         bitmapLetters[i].y = (H / 6 - 30) + (H / 6 - 30) * sinf(5 * demotimer + 0.004f * bitmapLetters[i].x);
         if (bitmapLetters[i].x < -100) {
            const int p = i > 0 ? i - 1 : NBBITMAPSCROLLINGLETTERS - 1;
            int delta;

            bitmapLetters[i].x = bitmapLetters[p].x + widthList[scrollerText[bitmapLetters[p].letter] - 32] +
                                 BITMAP_SCROLLER_LETTER_SPACING;
            bitmapLetters[i].letter += NBBITMAPSCROLLINGLETTERS;
            delta = (int)strlen(scrollerText) - bitmapLetters[i].letter;
            if (delta <= 0)
               bitmapLetters[i].letter = -delta;
         }
      }
      for (i = 0; i < nbRasters; i++) {
         rasters[i].thickness = 32;
         rasters[i].width = W;
         rasters[i].rotate = 0;
         rasters[i].y = 2 * H / 3 + H / 3 * sinf(2 * (demotimer + 0.05f * i));
         rasters[i].x = W / 2;
         raster_colours(i);
      }
      break;
   case 4:
      backgroundAngle += 0.05f * 0.1f * step;
      for (i = 0; i < nbRasters; i++) {
         rasters[i].thickness = 32;
         rasters[i].width = W;
         rasters[i].rotate = 20 * sinf(demotimer + 0.025f * i);
         rasters[i].y = H / 2 + H / 2 * sinf(2 * (demotimer + 0.05f * i));
         rasters[i].x = W / 2;
         raster_colours(i);
      }
      break;
   case 5:
      backgroundAngle += 0.05f * 0.02f * step;
      for (i = 0; i < nbRasters; i++) {
         rasters[i].thickness = 32;
         rasters[i].width = W / 2 + W / 4 * sinf(0.25f * sinf(0.15f * demotimer) * (demotimer + 0.5f * i));
         rasters[i].rotate = 70 * sinf(demotimer + 0.05f * i);
         rasters[i].y = H / 2 + H / 4 * sinf(2 * (demotimer + 0.05f * i));
         rasters[i].x = W / 2;
         raster_colours(i);
      }
      break;
   case 6:
      backgroundAngle += 0.05f * 0.02f * step;
      for (i = 0; i < nbRasters; i++) {
         rasters[i].thickness = H;
         rasters[i].width = 32;
         rasters[i].rotate = 0;
         rasters[i].y = H * (1 - (float)i / nbRasters);
         rasters[i].x = W / 2 + 1.5f * (W / 4) * sinf(2 * demotimer + 0.075f * i) * sinf(0.6f * demotimer + 0.03f * i);
         raster_colours(i);
      }
      /* fall through: the logo waves here too */
   case 7:
      if (phase == 7) {
         backgroundAngle = 360 * sinf(0.001f * demotimer);
         backgroundTile = 0.5f + 1.5f * fabsf(sinf(0.5f * demotimer));
         texShiftX += 0.5f * dt;
         texShiftY += 1.5f * dt;
      }
      for (i = 0; i <= SINELOGO_SUBTILES; i++)
         for (j = 0; j <= SINELOGO_SUBTILES; j++) {
            sineTiles[i][j].x = (W / 3 - 128) + i * 256 / SINELOGO_SUBTILES +
                                50 * sinf(3 * demotimer + 4.0f * j / SINELOGO_SUBTILES);
            sineTiles[i][j].y = (H / 2 - 128) + j * 256 / SINELOGO_SUBTILES +
                                50 * sinf(3 * demotimer + 4.0f * i / SINELOGO_SUBTILES);
         }
      break;
   case 8:
      for (i = 0; i < NB3DLETTERS; i++) {
         letters3d[i].y += 2.0f * dt;
         if (letters3d[i].y > 5.5f) {
            const int p = i > 0 ? i - 1 : NB3DLETTERS - 1;
            int delta;

            letters3d[i].y = letters3d[p].y - LETTER3D_SPACING;
            letters3d[i].letter += NB3DLETTERS;
            delta = (int)strlen(scrollerText) - letters3d[i].letter;
            if (delta <= 0)
               letters3d[i].letter = -delta;
         }
      }
      break;
   }
}

static void
events(float t)
{
   static float next_look;
   XEvent ev;

   /* follow the window: dtwm moves its frame without telling us */
   if (t >= next_look) {
      Window child;
      int nx, ny;

      next_look = t + 0.2f;
      if (XTranslateCoordinates(dpy, win, RootWindow(dpy, DefaultScreen(dpy)), 0, 0, &nx, &ny, &child) &&
          (nx != win_x || ny != win_y)) {
         win_x = nx;
         win_y = ny;
         zzgl_platform_place(nx, ny);
      }
   }
   while (XPending(dpy)) {
      XNextEvent(dpy, &ev);
      if (ev.type == KeyPress) {
         const KeySym k = XLookupKeysym(&ev.xkey, 0);

         if (k == XK_Escape || k == XK_q)
            quit(0);
      }
   }
}

static void
open_window(void)
{
   XSetWindowAttributes swa;
   XSizeHints hints;
   XEvent ev;
   Window child;
   int scr, x, y;

   dpy = XOpenDisplay(NULL);
   if (!dpy) {
      fprintf(stderr, "lamerintro: cannot open the X display\n");
      exit(1);
   }
   scr = DefaultScreen(dpy);
   swa.border_pixel = 0;
   swa.background_pixel = BlackPixel(dpy, scr);
   swa.event_mask = KeyPressMask | StructureNotifyMask | ExposureMask;
   win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0, CopyFromParent, InputOutput, CopyFromParent,
                       CWBorderPixel | CWBackPixel | CWEventMask, &swa);
   hints.flags = PSize | PMinSize | PMaxSize;
   hints.width = hints.min_width = hints.max_width = W;
   hints.height = hints.min_height = hints.max_height = H;
   XSetStandardProperties(dpy, win, "LamerIntro", "LamerIntro", None, NULL, 0, &hints);
   XMapWindow(dpy, win);
   do
      XNextEvent(dpy, &ev);
   while (ev.type != MapNotify);
   if (!XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &x, &y, &child))
      x = y = 0;
   win_x = x;
   win_y = y;
   if (zzgl_platform_open(dpy, win, x, y, W, H) != 0) {
      fprintf(stderr, "lamerintro: no GL on the Z3660's core 1\n");
      exit(1);
   }
}

int
main(int argc, char **argv)
{
   const char *e;
   const float start = (e = getenv("LAMER_START")) ? (float)atof(e) : 0;
   const float seconds = (e = getenv("LAMER_SECONDS")) ? (float)atof(e) : 0;
   float t0, last;
   long frames = 0;

   if ((e = getenv("LAMER_W")))
      W = atoi(e);
   if ((e = getenv("LAMER_H")))
      H = atoi(e);
   signal(SIGTERM, on_signal);
   signal(SIGINT, on_signal);
   open_window();
#define X(ret, name, args) \
   if (!(gl_##name = (ret (*) args)zzgl_GetProcAddress("gl" #name))) { \
      fprintf(stderr, "lamerintro: no gl%s\n", #name); \
      return 1; \
   }
   GL_FUNCS(X)
#undef X
   init_gl();
   start_music(argc > 1 ? argv[1] : "intro.xm");
   t0 = last = now();
   for (;;) {
      const float t = now();

      dt = t - last;
      if (dt > 0.5f)
         dt = 0.5f;
      last = t;
      demotimer = start + t - t0;
      events(t);
      if (module)
         MikMod_Update();
      update();
      draw_scene();
      frames++;
      if (seconds > 0 && t - t0 >= seconds) {
         printf("lamerintro: %ld frames in %.1f s: %.1f fps\n", frames, t - t0, frames / (t - t0));
         quit(0);
      }
   }
}
