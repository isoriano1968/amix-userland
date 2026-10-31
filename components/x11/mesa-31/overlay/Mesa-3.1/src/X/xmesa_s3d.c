/*
 * xmesa_s3d.c -- XMesa on the Z3660's soft3D (AMIX, 2026-10-26).
 *
 * Unless MESA_SOFT3D=0 is in the environment, a double-buffered RGB window gets
 * its back buffer in off-screen VRAM, and Mesa hands every triangle, line
 * and point -- transformed, lit, clipped and culled on the 68060 -- to the
 * Z3660's ARM rasterizer through libs3d (soft3d-amix/libs3d).  The swap is
 * a blitter copy by the X server (ZZ9K-SOFT3D), through the window's clip
 * list.  Drawing to the front buffer stays on Mesa's own X path.
 *
 * What soft3D does: Z (any GL depth function, depth mask), Gouraud and
 * flat shading, one 2D texture (level 0 of the bound object, any GL
 * format, converted to RGB or RGBA), the GL texture environments, every
 * GL blend factor pair, alpha test as alpha blending.  What it does not:
 * fog, stencil, logic ops, polygon stipple, multitexture beyond unit 0,
 * scissoring of triangles (glClear honours the scissor box) -- these are
 * ignored, the rest of the picture is right.
 *
 * glBitmap, glDrawPixels, glReadPixels and anything else Mesa draws itself
 * go through the span functions here, into the VRAM back buffer (after the
 * ARM has finished).  One soft3D buffer per machine at a time; the first
 * window that asks gets it.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>
#include "GL/xmesa.h"
#include "xmesaP.h"
#include "context.h"
#include "macros.h"
#include "types.h"
#include "vb.h"
#include "../../../soft3d-amix/libs3d/s3d.h"

static XMesaBuffer s3dbuf;              /* the buffer soft3D draws for */
static struct s3d_state base;           /* the state without the texture */
static int flat;

/* --- textures ------------------------------------------------------------- */

/* bumped by everything that can change the state a triangle needs (Mesa's
   UpdateState, texture images, deletions, a resize): use_state recomputes
   only then, not for each of Quake III's 8,500 triangles a frame */
static unsigned tex_epoch = 1;

struct tdata {
    struct s3d_tex *tex;
    const struct gl_texture_image *image;       /* what tex was made from */
    int dirty;
};

/* level 0 of a texture object as RGB or RGBA bytes, for libs3d */
static unsigned char *convert(const struct gl_texture_image *img,
                              const GLubyte *palette, int *bpp)
{
    int n = img->Width * img->Height, i, b;
    const GLubyte *s = img->Data;
    unsigned char *d, *p;

    switch (img->Format) {
    case GL_RGB:
    case GL_LUMINANCE:
        b = 3; break;
    default:
        b = 4; break;
    }
    if (!s || !(d = (unsigned char *) malloc((size_t) n * b)))
        return 0;
    for (i = 0, p = d; i < n; i++, p += b) {
        switch (img->Format) {
        case GL_RGB:
            p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; s += 3; break;
        case GL_RGBA:
            p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = s[3]; s += 4; break;
        case GL_LUMINANCE:
            p[0] = p[1] = p[2] = s[0]; s += 1; break;
        case GL_LUMINANCE_ALPHA:
            p[0] = p[1] = p[2] = s[0]; p[3] = s[1]; s += 2; break;
        case GL_ALPHA:
            p[0] = p[1] = p[2] = 255; p[3] = s[0]; s += 1; break;
        case GL_INTENSITY:
            p[0] = p[1] = p[2] = p[3] = s[0]; s += 1; break;
        case GL_COLOR_INDEX:
            if (palette) {
                const GLubyte *c = palette + s[0] * 4;
                p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = c[3];
            } else
                p[0] = p[1] = p[2] = p[3] = s[0];
            s += 1;
            break;
        default:
            p[0] = p[1] = p[2] = p[3] = 255; break;
        }
    }
    *bpp = b;
    return d;
}

/* the soft3D texture of the current 2D texture, made or refreshed */
/* unit 0 draws a 2D texture: ReallyEnabled from the raw state, so that it
   holds before gl_update_state too (completeness must be current: no dirty
   texture objects) */
static int tex2d(GLcontext *ctx)
{
    const struct gl_texture_unit *u = &ctx->Texture.Unit[0];
    if ((u->Enabled & TEXTURE0_3D) && u->CurrentD[3]->Complete)
        return 0;
    return (u->Enabled & TEXTURE0_2D) && u->CurrentD[2]->Complete;
}

static struct s3d_tex *current_texture(GLcontext *ctx)
{
    struct gl_texture_object *t;
    const struct gl_texture_image *img;
    struct tdata *td;
    unsigned char *texels;
    int bpp;

    if (!tex2d(ctx))
        return 0;
    t = ctx->Texture.Unit[0].CurrentD[2];
    if (!t || !(img = t->Image[t->BaseLevel]) || !img->Data)
        return 0;
    td = (struct tdata *) t->DriverData;
    if (!td) {
        if (!(td = (struct tdata *) calloc(1, sizeof *td)))
            return 0;
        td->dirty = 1;
        t->DriverData = td;
    }
    if (td->image != img)
        td->dirty = 1;
    if (!td->dirty)
        return td->tex;
    texels = convert(img, t->PaletteSize ? t->Palette : 0, &bpp);
    if (!texels)
        return td->tex;
    if (td->tex && (td->tex != 0) && td->image == img &&
        s3d_tex_update(td->tex, texels) == 0)
        ;
    else {
        s3d_tex_free(td->tex);
        td->tex = s3d_tex_new(img->Width, img->Height, bpp, texels);
    }
    free(texels);
    td->image = img;
    td->dirty = 0;
    return td->tex;
}

static void tex_image(GLcontext *ctx, GLenum target, struct gl_texture_object *t,
                      GLint level, GLint internalFormat, const struct gl_texture_image *image)
{
    struct tdata *td = (struct tdata *) t->DriverData;
    (void) ctx; (void) target; (void) internalFormat; (void) image;
    if (td && level == t->BaseLevel)
        td->dirty = 1;
    tex_epoch++;
}

static void tex_subimage(GLcontext *ctx, GLenum target, struct gl_texture_object *t,
                         GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                         GLint internalFormat, const struct gl_texture_image *image)
{
    struct tdata *td = (struct tdata *) t->DriverData;
    (void) ctx; (void) target; (void) x; (void) y; (void) w; (void) h;
    (void) internalFormat; (void) image;
    if (td && level == t->BaseLevel)
        td->dirty = 1;
    tex_epoch++;
}

static void delete_texture(GLcontext *ctx, struct gl_texture_object *t)
{
    struct tdata *td = (struct tdata *) t->DriverData;
    (void) ctx;
    if (td) {
        s3d_tex_free(td->tex);
        free(td);
        t->DriverData = 0;
    }
    tex_epoch++;
}

/* --- state -------------------------------------------------------------------- */

static void compute_state(GLcontext *ctx)
{
    int s, d;

    base.tex = 0;
    if (ctx->Depth.Test)
        base.zmode = S3D_ZMODE(ctx->Depth.Mask ? 1 : 0, s3d_gl_depthfunc(ctx->Depth.Func));
    else
        base.zmode = S3D_ZMODE(0, S3D_Z_ALWAYS);
    base.blend = S3D_REPLACE;
    if (ctx->Color.BlendEnabled) {
        s = s3d_gl_factor(ctx->Color.BlendSrcRGB);
        d = s3d_gl_factor(ctx->Color.BlendDstRGB);
        if (s && d)
            base.blend = S3D_BLEND(s, d);
    } else if (ctx->Color.AlphaEnabled && ctx->Color.AlphaFunc != GL_ALWAYS)
        base.blend = S3D_BLEND(7, 8);           /* SRC_ALPHA, ONE_MINUS_SRC_ALPHA */
    base.texenv = s3d_gl_texenv(ctx->Texture.Unit[0].EnvMode);
    flat = ctx->Light.ShadeModel == GL_FLAT;
}

static void use_state(GLcontext *ctx, int textured)
{
    static unsigned seen;
    static struct s3d_state with_tex, without_tex;
    static const struct s3d_state *last;
    const struct s3d_state *want;

    if (seen != tex_epoch) {
        with_tex = without_tex = base;
        with_tex.tex = current_texture(ctx);
        seen = tex_epoch;
        last = 0;
    }
    want = textured ? &with_tex : &without_tex;
    if (want != last) {
        s3d_set_state(want);
        last = want;
    }
}

/* --- the vertex cache --------------------------------------------------------
 * Mesa calls RasterSetup whenever it has put new window coordinates into a
 * vertex buffer (after the transform, and for the vertices clipping makes);
 * each vertex is converted for soft3D there, once, and the triangle, quad and
 * line functions take the converted ones by index.  The cache belongs to one
 * vertex buffer and its colour and texture coordinate arrays; anything else
 * (two-sided lighting switching colours, another buffer) converts directly.
 */
static struct s3d_vertex *sv;
static unsigned *sv_gen;        /* fill a vertex was converted in */
static unsigned gen = 1;
static GLuint sv_cap;
static int nocache = -1;        /* the cache is off unless MESA_S3D_CACHE=1 */
static struct vertex_buffer *sv_vb;
static const void *sv_color, *sv_tex;

static void convert_vertex(GLcontext *ctx, struct vertex_buffer *VB, struct s3d_vertex *o, GLuint i);

static void raster_setup(struct vertex_buffer *VB, GLuint start, GLuint end)
{
    GLcontext *ctx = VB->ctx;
    GLuint i;

    if (nocache < 0) {
        /* off unless MESA_S3D_CACHE=1: it draws Quake III's arrays wrong
           and saved nothing there (2026-10-27) */
        const char *e = getenv("MESA_S3D_CACHE");
        nocache = !(e && atoi(e) > 0);
    }
    if (nocache)
        return;
    if (end > sv_cap) {
        GLuint cap = end + 64;
        struct s3d_vertex *n = (struct s3d_vertex *) realloc(sv, cap * sizeof *sv);
        unsigned *g = (unsigned *) realloc(sv_gen, cap * sizeof *sv_gen);
        if (!n || !g) {
            if (n) sv = n;
            if (g) sv_gen = g;
            sv_vb = 0;
            return;
        }
        sv = n;
        sv_gen = g;
        memset(sv_gen + sv_cap, 0, (cap - sv_cap) * sizeof *sv_gen);
        sv_cap = cap;
    }
    /* the main call also covers the vertices Mesa copied down from the
       previous buffer (below CopyStart) */
    if (start == VB->CopyStart)
        start = 0;
    if (VB != sv_vb || (const void *) VB->ColorPtr != sv_color ||
        (const void *) VB->TexCoordPtr[0] != sv_tex) {
        sv_vb = VB;
        sv_color = VB->ColorPtr;
        sv_tex = VB->TexCoordPtr[0];
        start = 0;
    }
    /* a new fill: what earlier fills (and their clipped vertices) left above
       end is stale */
    if (start == 0 && ++gen == 0) {
        memset(sv_gen, 0, sv_cap * sizeof *sv_gen);
        gen = 1;
    }
    for (i = start; i < end; i++) {
        convert_vertex(ctx, VB, &sv[i], i);
        sv_gen[i] = gen;
    }
}

/* the converted vertex i of the current buffer, or 0 */
static const struct s3d_vertex *cached(GLcontext *ctx, GLuint i)
{
    struct vertex_buffer *VB = ctx->VB;
    if (VB == sv_vb && i < sv_cap && sv_gen[i] == gen && (const void *) VB->ColorPtr == sv_color &&
        (const void *) VB->TexCoordPtr[0] == sv_tex)
        return &sv[i];
    return 0;
}

/* --- primitives ------------------------------------------------------------- */

static void convert_vertex(GLcontext *ctx, struct vertex_buffer *VB, struct s3d_vertex *o, GLuint i)
{
    const GLfloat *w = VB->Win.data[i];
    const GLubyte *c = VB->ColorPtr->data[i];
    (void) ctx;

    o->x = w[0];
    o->y = (GLfloat) s3dbuf->height - w[1];
    o->z = w[2] * (1.0F / DEPTH_SCALE);
    o->w = w[3];
    if (VB->TexCoordPtr[0]) {
        const GLfloat *t = VB->TexCoordPtr[0]->data[i];
        o->u = t[0];
        o->v = VB->TexCoordPtr[0]->size > 1 ? t[1] : 0.0F;
        if (VB->TexCoordPtr[0]->size == 4 && t[3] != 0.0F && t[3] != 1.0F) {
            o->u /= t[3];
            o->v /= t[3];
        }
    } else
        o->u = o->v = 0.0F;
    o->r = c[0]; o->g = c[1]; o->b = c[2]; o->a = c[3];
}

/* vertex i for a primitive: from the cache when it can, with the provoking
   vertex's colour when flat, moved by zoff (depth units) */
static void vertex(GLcontext *ctx, struct s3d_vertex *o, GLuint i, GLuint pv, GLfloat zoff)
{
    const struct s3d_vertex *c = cached(ctx, i);

    if (c)
        *o = *c;
    else
        convert_vertex(ctx, ctx->VB, o, i);
    if (flat && pv != i) {
        const GLubyte *col = ctx->VB->ColorPtr->data[pv];
        o->r = col[0]; o->g = col[1]; o->b = col[2]; o->a = col[3];
    }
    if (zoff != 0.0F)
        o->z += zoff * (1.0F / DEPTH_SCALE);
}

/* Back-face culling: with the software rasterizer Mesa leaves it to the
   triangle functions (context.c drops DD_TRI_CULL; triangle.c, tritemp.h
   test backface_sign), so ours must test too.  c is twice the signed area
   in Mesa's window coordinates (y up), as Mesa computes it. */
static int culled(GLcontext *ctx, GLfloat c)
{
    if (!ctx->Polygon.CullFlag)
        return 0;
    if (ctx->Polygon.CullFaceMode == GL_FRONT_AND_BACK)
        return 1;
    return c * ctx->backface_sign > 0;
}

static GLfloat tri_area(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2)
{
    GLfloat (*win)[4] = ctx->VB->Win.data;
    return (win[v1][0] - win[v0][0]) * (win[v2][1] - win[v0][1]) -
           (win[v1][1] - win[v0][1]) * (win[v2][0] - win[v0][0]);
}

static void s3_triangle(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2, GLuint pv)
{
    struct s3d_vertex a, b, c;
    GLfloat zoff = ctx->Polygon.OffsetFill ? ctx->PolygonZoffset : 0.0F;
    const struct s3d_vertex *p0, *p1, *p2;

    if (culled(ctx, tri_area(ctx, v0, v1, v2)))
        return;
    use_state(ctx, 1);
    /* the common case: smooth, no offset, all three cached -- no copies */
    if (!flat && zoff == 0.0F && (p0 = cached(ctx, v0)) && (p1 = cached(ctx, v1)) &&
        (p2 = cached(ctx, v2))) {
        s3d_tri(p0, p1, p2);
        return;
    }
    vertex(ctx, &a, v0, pv, zoff);
    vertex(ctx, &b, v1, pv, zoff);
    vertex(ctx, &c, v2, pv, zoff);
    s3d_tri(&a, &b, &c);
}

static void s3_quad(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2, GLuint v3, GLuint pv)
{
    struct s3d_vertex a, b, c, d;
    GLfloat zoff = ctx->Polygon.OffsetFill ? ctx->PolygonZoffset : 0.0F;
    GLfloat (*win)[4] = ctx->VB->Win.data;

    /* facing from the diagonals, as Mesa's quad function */
    if (culled(ctx, (win[v2][0] - win[v0][0]) * (win[v3][1] - win[v1][1]) -
                    (win[v2][1] - win[v0][1]) * (win[v3][0] - win[v1][0])))
        return;
    use_state(ctx, 1);
    vertex(ctx, &a, v0, pv, zoff);
    vertex(ctx, &b, v1, pv, zoff);
    vertex(ctx, &c, v2, pv, zoff);
    vertex(ctx, &d, v3, pv, zoff);
    s3d_tri(&a, &b, &d);
    s3d_tri(&b, &c, &d);
}

/* a line: a quad, ctx->Line.Width wide, untextured */
static void s3_line(GLcontext *ctx, GLuint v0, GLuint v1, GLuint pv)
{
    struct s3d_vertex a, b, q[4];
    GLfloat dx, dy, len, nx, ny, half = ctx->Line.Width * 0.5F;
    int i;

    if (half < 0.5F)
        half = 0.5F;
    use_state(ctx, 0);
    vertex(ctx, &a, v0, pv, ctx->LineZoffset);
    vertex(ctx, &b, v1, pv, ctx->LineZoffset);
    dx = b.x - a.x;
    dy = b.y - a.y;
    len = dx * dx + dy * dy;
    if (len < 0.0001F)
        return;
    len = half / (GLfloat) sqrt(len);
    nx = -dy * len;
    ny = dx * len;
    for (i = 0; i < 4; i++)
        q[i] = i < 2 ? a : b;
    q[0].x -= nx; q[0].y -= ny;
    q[1].x += nx; q[1].y += ny;
    q[2].x += nx; q[2].y += ny;
    q[3].x -= nx; q[3].y -= ny;
    s3d_tri(&q[0], &q[1], &q[2]);
    s3d_tri(&q[0], &q[2], &q[3]);
}

/* points: one triangle covering the ctx->Point.Size square */
static void s3_points(GLcontext *ctx, GLuint first, GLuint last)
{
    struct vertex_buffer *VB = ctx->VB;
    struct s3d_vertex p, q[4];
    GLfloat h = ctx->Point.Size * 0.5F;
    GLuint i;

    if (h < 0.5F)
        h = 0.5F;
    use_state(ctx, 0);
    /* a square the point's size around its centre, as two triangles (one
       bigger triangle showed as a little triangle, not a dot) */
    for (i = first; i <= last; i++)
        if (VB->ClipMask[i] == 0) {
            vertex(ctx, &p, i, i, ctx->PointZoffset);
            q[0] = q[1] = q[2] = q[3] = p;
            q[0].x -= h; q[0].y -= h;
            q[1].x += h; q[1].y -= h;
            q[2].x += h; q[2].y += h;
            q[3].x -= h; q[3].y += h;
            s3d_tri(&q[0], &q[1], &q[2]);
            s3d_tri(&q[0], &q[2], &q[3]);
        }
}

/* --- clear ------------------------------------------------------------------- */

static GLbitfield s3_clear(GLcontext *ctx, GLbitfield mask, GLboolean all,
                        GLint x, GLint y, GLint width, GLint height)
{
    if (mask & GL_COLOR_BUFFER_BIT) {
        const XMesaContext xmesa = (XMesaContext) ctx->DriverCtx;
        GLubyte r = (GLubyte) (ctx->Color.ClearColor[0] * 255.0F);
        GLubyte g = (GLubyte) (ctx->Color.ClearColor[1] * 255.0F);
        GLubyte b = (GLubyte) (ctx->Color.ClearColor[2] * 255.0F);
        unsigned short rgb = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        (void) xmesa;
        if (all)
            s3d_clear_color(0, 0, s3dbuf->width, s3dbuf->height, rgb);
        else
            s3d_clear_color(x, (GLint) s3dbuf->height - y - height, width, height, rgb);
        mask &= ~GL_COLOR_BUFFER_BIT;
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        /* soft3D's Z only: Mesa's own depth buffer is not used here, and
           clearing it would cost the 68060 a w*h*2-byte fill a frame */
        s3d_clear_depth((GLfloat) ctx->Depth.Clear);
        mask &= ~GL_DEPTH_BUFFER_BIT;
    }
    return mask;
}

/* --- spans: the VRAM back buffer, RGB565, y up in Mesa's coordinates ------- */

static unsigned short *s3_row(GLint y)
{
    int stride;
    unsigned short *p;

    s3d_finish();
    p = s3d_backbuffer(&stride);
    if (!p || y < 0 || y >= (GLint) s3dbuf->height)
        return 0;
    return p + (s3dbuf->height - 1 - y) * stride;
}

#define PACK(r, g, b) ((unsigned short) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

static void write_rgba_span(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                            CONST GLubyte rgba[][4], const GLubyte mask[])
{
    unsigned short *p = s3_row(y);
    GLuint i;
    (void) ctx;
    if (!p)
        return;
    for (i = 0; i < n; i++, x++)
        if ((!mask || mask[i]) && x >= 0 && x < (GLint) s3dbuf->width)
            p[x] = PACK(rgba[i][0], rgba[i][1], rgba[i][2]);
}

static void write_rgb_span(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                           CONST GLubyte rgb[][3], const GLubyte mask[])
{
    unsigned short *p = s3_row(y);
    GLuint i;
    (void) ctx;
    if (!p)
        return;
    for (i = 0; i < n; i++, x++)
        if ((!mask || mask[i]) && x >= 0 && x < (GLint) s3dbuf->width)
            p[x] = PACK(rgb[i][0], rgb[i][1], rgb[i][2]);
}

static unsigned short mono;

static void set_color(GLcontext *ctx, GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    (void) ctx; (void) a;
    mono = PACK(r, g, b);
}

static void write_mono_span(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                            const GLubyte mask[])
{
    unsigned short *p = s3_row(y);
    GLuint i;
    (void) ctx;
    if (!p)
        return;
    for (i = 0; i < n; i++, x++)
        if ((!mask || mask[i]) && x >= 0 && x < (GLint) s3dbuf->width)
            p[x] = mono;
}

static void write_rgba_pixels(const GLcontext *ctx, GLuint n, const GLint x[], const GLint y[],
                              CONST GLubyte rgba[][4], const GLubyte mask[])
{
    GLuint i;
    unsigned short *p;
    (void) ctx;
    for (i = 0; i < n; i++)
        if ((!mask || mask[i]) && (p = s3_row(y[i])) && x[i] >= 0 && x[i] < (GLint) s3dbuf->width)
            p[x[i]] = PACK(rgba[i][0], rgba[i][1], rgba[i][2]);
}

static void write_mono_pixels(const GLcontext *ctx, GLuint n, const GLint x[], const GLint y[],
                              const GLubyte mask[])
{
    GLuint i;
    unsigned short *p;
    (void) ctx;
    for (i = 0; i < n; i++)
        if ((!mask || mask[i]) && (p = s3_row(y[i])) && x[i] >= 0 && x[i] < (GLint) s3dbuf->width)
            p[x[i]] = mono;
}

static void unpack(unsigned short c, GLubyte *o)
{
    o[0] = (GLubyte) (((c >> 11) & 31) * 255 / 31);
    o[1] = (GLubyte) (((c >> 5) & 63) * 255 / 63);
    o[2] = (GLubyte) ((c & 31) * 255 / 31);
    o[3] = 255;
}

static void read_rgba_span(const GLcontext *ctx, GLuint n, GLint x, GLint y, GLubyte rgba[][4])
{
    unsigned short *p = s3_row(y);
    GLuint i;
    (void) ctx;
    for (i = 0; i < n; i++, x++)
        if (p && x >= 0 && x < (GLint) s3dbuf->width)
            unpack(p[x], rgba[i]);
        else
            rgba[i][0] = rgba[i][1] = rgba[i][2] = rgba[i][3] = 0;
}

static void read_rgba_pixels(const GLcontext *ctx, GLuint n, const GLint x[], const GLint y[],
                             GLubyte rgba[][4], const GLubyte mask[])
{
    GLuint i;
    unsigned short *p;
    (void) ctx;
    for (i = 0; i < n; i++)
        if ((!mask || mask[i]) && (p = s3_row(y[i])) && x[i] >= 0 && x[i] < (GLint) s3dbuf->width)
            unpack(p[x[i]], rgba[i]);
}

static void s3_finish(GLcontext *ctx)
{
    (void) ctx;
    s3d_finish();
}

/* --- the XMesa hooks --------------------------------------------------------- */

/* from XMesaMakeCurrent: does this buffer draw with soft3D? */
void xmesa_s3d_attach(XMesaBuffer b)
{
    const char *e = getenv("MESA_SOFT3D");

    /* on by default since 2026-10-27; MESA_SOFT3D=0 keeps Mesa on the 68k,
       MESA_SOFT3D=1 also says why soft3D could not be used */
    if (s3dbuf || (e && atoi(e) <= 0) || !b->db_state || b->pixmap_flag)
        return;
    s3d_verbose(e && atoi(e) > 0);
    if (!b->xm_visual->gl_visual->RGBAflag)
        return;
    if (s3d_init(b->display, "Mesa") < 0)
        return;
    if (s3d_resize(b->width ? b->width : 1, b->height ? b->height : 1) < 0)
        return;
    s3dbuf = b;
}

int xmesa_s3d_buffer(XMesaBuffer b)
{
    return b && b == s3dbuf && s3d_active();
}

/* from get_buffer_size, when the window's size changed */
void xmesa_s3d_resize(XMesaBuffer b)
{
    if (xmesa_s3d_buffer(b)) {
        s3d_resize(b->width, b->height);
        tex_epoch++;                    /* soft3D's state is gone */
        sv_vb = 0;                      /* the height went into y */
    }
}

/* from XMesaSwapBuffers: 1 if done */
int xmesa_s3d_swap(XMesaBuffer b)
{
    if (!xmesa_s3d_buffer(b))
        return 0;
    s3d_swap(b->display, b->frontbuffer);
    return 1;
}

/* from xmesa_update_state, last: soft3D's functions while drawing to the
   back buffer of the soft3D window */
void xmesa_s3d_update_state(GLcontext *ctx)
{
    const XMesaContext xmesa = (XMesaContext) ctx->DriverCtx;
    XMesaBuffer b = xmesa->xm_buffer;

    if (!xmesa_s3d_buffer(b) || b->buffer == b->frontbuffer) {
        if (ctx->Driver.RasterSetup == raster_setup)
            ctx->Driver.RasterSetup = 0;
        return;
    }
    compute_state(ctx);
    tex_epoch++;
    ctx->Driver.RasterSetup = raster_setup;
    ctx->Driver.TriangleFunc = s3_triangle;
    ctx->Driver.QuadFunc = s3_quad;
    ctx->Driver.LineFunc = s3_line;
    ctx->Driver.PointsFunc = s3_points;
    ctx->Driver.Clear = s3_clear;
    ctx->Driver.Finish = s3_finish;
    ctx->Driver.Color = set_color;
    ctx->Driver.WriteRGBASpan = write_rgba_span;
    ctx->Driver.WriteRGBSpan = write_rgb_span;
    ctx->Driver.WriteMonoRGBASpan = write_mono_span;
    ctx->Driver.WriteRGBAPixels = write_rgba_pixels;
    ctx->Driver.WriteMonoRGBAPixels = write_mono_pixels;
    ctx->Driver.ReadRGBASpan = read_rgba_span;
    ctx->Driver.ReadRGBAPixels = read_rgba_pixels;
    ctx->Driver.TexImage = tex_image;
    ctx->Driver.TexSubImage = tex_subimage;
    ctx->Driver.DeleteTexture = delete_texture;
}

/* --- for renderers that feed soft3D themselves --------------------------------
 * ioquake3's renderer (renderergl1/tr_amix_s3d.c) transforms and clips its
 * own vertex arrays and hands the triangles to libs3d (s3d_tri), skipping
 * Mesa's pipeline; the GL state (blending, depth, texture environment, the
 * bound texture) still comes from Mesa.  glAmixS3DBegin brings soft3D's
 * state up to date with it and says whether soft3D is drawing at all.
 */
#define LEAN_STATE (NEW_RASTER_OPS | NEW_TEXTURING | NEW_TEXTURE_ENABLE | NEW_TEXTURE_ENV | \
                    NEW_CLIENT_STATE | NEW_POLYGON | NEW_MODELVIEW | NEW_PROJECTION | NEW_VIEWPORT)

int glAmixS3DBegin(void)
{
    GLcontext *ctx = gl_get_current_context();
    XMesaContext xmesa;

    if (!ctx || !ctx->DriverCtx)
        return 0;
    xmesa = (XMesaContext) ctx->DriverCtx;
    if (!xmesa_s3d_buffer(xmesa->xm_buffer) || xmesa->xm_buffer->buffer == xmesa->xm_buffer->frontbuffer)
        return 0;
    FLUSH_VB(ctx, "glAmixS3DBegin");
    /* Quake III changes blending, depth and the texture between almost every
       draw; soft3D needs only those, and gl_update_state (lighting, pipeline,
       clipping...) costs far more than the draw.  It is left pending for
       Mesa's own drawing. */
    if (ctx->NewState) {
        if (!(ctx->NewState & ~LEAN_STATE) && !ctx->Shared->DirtyTexObjList) {
            compute_state(ctx);
            tex_epoch++;
        } else
            gl_update_state(ctx);
    }
    use_state(ctx, tex2d(ctx));
    return 1;
}

/* for programs: does a window draw with soft3D (attached at MakeCurrent)? */
int glAmixSoft3D(void)
{
    return s3dbuf != 0;
}

int glAmixS3DHeight(void)
{
    return s3dbuf ? (int) s3dbuf->height : 0;
}
