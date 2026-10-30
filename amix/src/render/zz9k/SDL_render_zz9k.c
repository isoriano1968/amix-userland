/*
  Simple DirectMedia Layer: the "zz9k" renderer for AMIX, drawing with
  soft3D (Wazp3D on the Z3660's ARM).
  Copyright (C) 2026 isoriano1968; zlib licence, as SDL.

  Textures live in the board's memory, every drawing command becomes
  triangles that the ARM rasterizes into a back buffer in off-screen video
  memory, and SDL_RenderPresent has the X server copy the back buffer into
  the window (the ZZ9K-SOFT3D request of Xzz9000).  So a texture scaled to
  a big window or to the whole screen costs the 68060 only the texels it
  writes, not the pixels on the screen.

  - IYUV and YV12 are textures of their own: the frame is converted to RGBA
    straight into board memory, once, with tables.
  - soft3D takes only triangles inside its drawing area (s3d.c checks them),
    so everything is clipped here, to the viewport, the clip rectangle and
    the window, with u, v and colours interpolated along.
  - Opaque rectangles, points and straight lines go to the blitter.
  - One soft3D user at a time on the machine: when it is busy, or the X
    server, the kernel or the board cannot do it, SDL falls back to the
    software renderer.  SDL_RENDER_DRIVER=software chooses that directly.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_ZZ9K

#include "SDL_hints.h"
#include "SDL_syswm.h"
#include "../SDL_sysrender.h"
#include "s3d.h"

#define ZZ9K_MAX_TEXTURE 2048

typedef struct
{
    Display *display;
    Window xwindow;
    int w, h;                   /* soft3D's drawing area: the window */
    SDL_Rect viewport;
    SDL_bool clipping;
    SDL_Rect cliprect;          /* in the viewport */
    SDL_bool dirty;             /* drawn since the last present */
} ZZ9K_RenderData;

typedef struct
{
    struct s3d_tex *tex;
    int bpp;                    /* texels in board memory: 3 or 4 bytes */
    SDL_bool yuv;               /* IYUV or YV12, converted into the texels */
    Uint8 *staging;             /* LockTexture's pixels, in RAM */
    int pitch;
    SDL_Rect locked;
} ZZ9K_TextureData;

/* --- YUV to RGBA ---------------------------------------------------------- */

#define TOFF 384                        /* index offset of the clamp tables */
static Uint32 r32[1024], g32[1024], b32[1024];
static int ytab[256], rvtab[256], gutab[256], gvtab[256], butab[256];
static int tables_mode = -1;

static void yuv_tables(SDL_YUV_CONVERSION_MODE mode)
{
    double ys, cs, yo, kr, kgu, kgv, kb;
    int i, c;

    if ((int)mode == tables_mode) {
        return;
    }
    if (tables_mode < 0) {
        for (i = 0; i < 1024; i++) {
            c = i - TOFF;
            c = c < 0 ? 0 : c > 255 ? 255 : c;
            r32[i] = (Uint32)c << 24;   /* R G B A in memory: RGBA8888 on the 68k */
            g32[i] = (Uint32)c << 16;
            b32[i] = (Uint32)c << 8 | 0xff;
        }
    }
    tables_mode = (int)mode;
    if (mode == SDL_YUV_CONVERSION_JPEG) {
        ys = cs = 1.0;
        yo = 0;
    } else {
        ys = 255.0 / 219;
        cs = 255.0 / 224;
        yo = 16;
    }
    if (mode == SDL_YUV_CONVERSION_BT709) {
        kr = 1.5748; kgu = 0.187324; kgv = 0.468124; kb = 1.8556;
    } else {
        kr = 1.402; kgu = 0.344136; kgv = 0.714136; kb = 1.772;
    }
    for (i = 0; i < 256; i++) {
        double d = (i - 128) * cs, rnd = i >= 128 ? 0.5 : -0.5;

        ytab[i] = (int)((i - yo) * ys + 0.5 + TOFF) - TOFF;
        rvtab[i] = (int)(d * kr + rnd);
        gutab[i] = (int)(d * kgu + rnd);
        gvtab[i] = (int)(d * kgv + rnd);
        butab[i] = (int)(d * kb + rnd);
    }
}

/* the rectangle r (even x, y, w, h) of a 4:2:0 picture into the texels */
static void yuv_rgba(SDL_Texture *texture, Uint32 *texels, const SDL_Rect *r,
                     const Uint8 *Y, int Ypitch, const Uint8 *U, int Upitch, const Uint8 *V, int Vpitch)
{
    int x, y, w2 = r->w / 2;

    yuv_tables(SDL_GetYUVConversionModeForResolution(texture->w, texture->h));
    for (y = 0; y < r->h; y += 2) {
        const Uint8 *y0 = Y + (size_t)y * Ypitch, *y1 = y0 + Ypitch;
        const Uint8 *u = U + (size_t)(y / 2) * Upitch;
        const Uint8 *v = V + (size_t)(y / 2) * Vpitch;
        Uint32 *d0 = texels + (size_t)(r->y + y) * texture->w + r->x;
        Uint32 *d1 = d0 + texture->w;
        SDL_bool last = (y + 1 >= r->h);

        for (x = 0; x < w2; x++) {
            int cv = *v++, cu = *u++, a;
            const Uint32 *rt = r32 + TOFF + rvtab[cv];
            const Uint32 *gt = g32 + TOFF - gutab[cu] - gvtab[cv];
            const Uint32 *bt = b32 + TOFF + butab[cu];

            a = ytab[*y0++]; *d0++ = rt[a] | gt[a] | bt[a];
            a = ytab[*y0++]; *d0++ = rt[a] | gt[a] | bt[a];
            if (!last) {
                a = ytab[*y1++]; *d1++ = rt[a] | gt[a] | bt[a];
                a = ytab[*y1++]; *d1++ = rt[a] | gt[a] | bt[a];
            }
        }
    }
}

/* --- textures ------------------------------------------------------------- */

static SDL_bool ZZ9K_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
    case SDL_BLENDMODE_BLEND:
    case SDL_BLENDMODE_ADD:
    case SDL_BLENDMODE_MOD:
    case SDL_BLENDMODE_MUL:
        return SDL_TRUE;
    default:
        return SDL_FALSE;
    }
}

static int ZZ9K_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    ZZ9K_TextureData *td;
    Uint8 *zero;
    size_t stage;

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        return SDL_Unsupported();
    }
    td = (ZZ9K_TextureData *)SDL_calloc(1, sizeof(*td));
    if (!td) {
        return SDL_OutOfMemory();
    }
    switch (texture->format) {
    case SDL_PIXELFORMAT_RGBA8888:
        td->bpp = 4;
        td->pitch = texture->w * 4;
        stage = (size_t)td->pitch * texture->h;
        break;
    case SDL_PIXELFORMAT_RGB24:
        td->bpp = 3;
        td->pitch = texture->w * 3;
        stage = (size_t)td->pitch * texture->h;
        break;
    case SDL_PIXELFORMAT_IYUV:
    case SDL_PIXELFORMAT_YV12:
        if ((texture->w | texture->h) & 1) {
            SDL_free(td);
            return SDL_SetError("zz9k: YUV textures need an even size");
        }
        td->bpp = 4;
        td->yuv = SDL_TRUE;
        td->pitch = texture->w;
        stage = (size_t)texture->w * texture->h * 3 / 2;
        break;
    default:
        SDL_free(td);
        return SDL_SetError("zz9k: unsupported texture format");
    }
    zero = (Uint8 *)SDL_calloc(1, (size_t)texture->w * texture->h * td->bpp);
    if (!zero) {
        SDL_free(td);
        return SDL_OutOfMemory();
    }
    td->tex = s3d_tex_new(texture->w, texture->h, td->bpp, zero);
    SDL_free(zero);
    if (!td->tex) {
        SDL_free(td);
        return SDL_SetError("zz9k: no board memory for a %dx%d texture", texture->w, texture->h);
    }
    if (texture->access == SDL_TEXTUREACCESS_STREAMING) {
        td->staging = (Uint8 *)SDL_calloc(1, stage);
        if (!td->staging) {
            s3d_tex_free(td->tex);
            SDL_free(td);
            return SDL_OutOfMemory();
        }
    }
    texture->driverdata = td;
    return 0;
}

static int ZZ9K_UpdateTextureYUV(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect,
                                 const Uint8 *Yplane, int Ypitch,
                                 const Uint8 *Uplane, int Upitch,
                                 const Uint8 *Vplane, int Vpitch)
{
    ZZ9K_TextureData *td = (ZZ9K_TextureData *)texture->driverdata;
    SDL_Rect r = *rect;
    Uint8 *px;

    if (!td->yuv) {
        return SDL_SetError("zz9k: not a YUV texture");
    }
    /* 2x2 blocks: an even corner (an odd width or height loses nothing,
       the texture's own size is even) */
    if ((r.x | r.y) & 1) {
        return SDL_SetError("zz9k: YUV update at an odd position");
    }
    r.w = (r.w + 1) & ~1;
    r.h = (r.h + 1) & ~1;
    s3d_tex_wait();             /* nothing drawn still reads it */
    px = s3d_tex_pixels(td->tex);
    if (!px) {
        return SDL_SetError("zz9k: texture not mapped");
    }
    yuv_rgba(texture, (Uint32 *)px, &r, Yplane, Ypitch, Uplane, Upitch, Vplane, Vpitch);
    s3d_tex_touch(td->tex);
    return 0;
}

static int ZZ9K_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *rect, const void *pixels, int pitch)
{
    ZZ9K_TextureData *td = (ZZ9K_TextureData *)texture->driverdata;
    const Uint8 *src = (const Uint8 *)pixels;
    Uint8 *px;
    int y, row;

    if (td->yuv) {
        const Uint8 *Y = src, *U, *V;
        int cpitch = (pitch + 1) / 2;

        U = Y + (size_t)rect->h * pitch;
        V = U + (size_t)((rect->h + 1) / 2) * cpitch;
        if (texture->format == SDL_PIXELFORMAT_YV12) {
            const Uint8 *t = U;
            U = V;
            V = t;
        }
        return ZZ9K_UpdateTextureYUV(renderer, texture, rect, Y, pitch, U, cpitch, V, cpitch);
    }
    s3d_tex_wait();
    px = s3d_tex_pixels(td->tex);
    if (!px) {
        return SDL_SetError("zz9k: texture not mapped");
    }
    row = rect->w * td->bpp;
    px += ((size_t)rect->y * texture->w + rect->x) * td->bpp;
    for (y = 0; y < rect->h; y++) {
        SDL_memcpy(px, src, row);
        px += (size_t)texture->w * td->bpp;
        src += pitch;
    }
    s3d_tex_touch(td->tex);
    return 0;
}

static int ZZ9K_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_Rect *rect, void **pixels, int *pitch)
{
    ZZ9K_TextureData *td = (ZZ9K_TextureData *)texture->driverdata;

    if (!td->staging) {
        return SDL_SetError("zz9k: texture is not streaming");
    }
    td->locked = *rect;
    if (td->yuv) {
        *pixels = td->staging + (size_t)rect->y * td->pitch + rect->x;
    } else {
        *pixels = td->staging + (size_t)rect->y * td->pitch + rect->x * td->bpp;
    }
    *pitch = td->pitch;
    return 0;
}

static void ZZ9K_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    ZZ9K_TextureData *td = (ZZ9K_TextureData *)texture->driverdata;
    const SDL_Rect *r = &td->locked;

    if (td->yuv) {
        /* the staging planes are the whole picture's (SDL_LockTexture's layout) */
        const Uint8 *Y = td->staging, *U, *V;
        int cw = texture->w / 2;

        U = Y + (size_t)texture->w * texture->h;
        V = U + (size_t)cw * (texture->h / 2);
        if (texture->format == SDL_PIXELFORMAT_YV12) {
            const Uint8 *t = U;
            U = V;
            V = t;
        }
        ZZ9K_UpdateTextureYUV(renderer, texture, r,
                              Y + (size_t)r->y * texture->w + r->x, texture->w,
                              U + (size_t)(r->y / 2) * cw + r->x / 2, cw,
                              V + (size_t)(r->y / 2) * cw + r->x / 2, cw);
    } else {
        ZZ9K_UpdateTexture(renderer, texture, r,
                           td->staging + (size_t)r->y * td->pitch + r->x * td->bpp, td->pitch);
    }
}

static void ZZ9K_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    /* soft3D samples as its preferences say */
}

static int ZZ9K_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return texture ? SDL_Unsupported() : 0;
}

static void ZZ9K_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    ZZ9K_TextureData *td = (ZZ9K_TextureData *)texture->driverdata;

    if (td) {
        s3d_tex_free(td->tex);
        SDL_free(td->staging);
        SDL_free(td);
        texture->driverdata = NULL;
    }
}

/* --- the command queue ---------------------------------------------------- */

/* every primitive is queued as triangles of these (window coordinates come
   with the viewport added when the queue runs) */
typedef struct
{
    float x, y, u, v;
    Uint8 r, g, b, a;
} ZZ9K_Vertex;

static int ZZ9K_QueueNoop(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return 0;
}

static int ZZ9K_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    SDL_FPoint *verts = (SDL_FPoint *)SDL_AllocateRenderVertices(renderer, count * sizeof(SDL_FPoint), 0, &cmd->data.draw.first);

    if (!verts) {
        return -1;
    }
    cmd->data.draw.count = count;
    SDL_memcpy(verts, points, count * sizeof(SDL_FPoint));
    return 0;
}

static int ZZ9K_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FRect *rects, int count)
{
    SDL_FRect *verts = (SDL_FRect *)SDL_AllocateRenderVertices(renderer, count * sizeof(SDL_FRect), 0, &cmd->data.draw.first);

    if (!verts) {
        return -1;
    }
    cmd->data.draw.count = count;
    SDL_memcpy(verts, rects, count * sizeof(SDL_FRect));
    return 0;
}

static void zz9k_quad(ZZ9K_Vertex *v, float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1, SDL_Color c)
{
    int i;

    /* two triangles: 0 1 2, 0 2 3 */
    float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };

    for (i = 0; i < 6; i++) {
        int k = order[i];
        v[i].x = xs[k];
        v[i].y = ys[k];
        v[i].u = us[k];
        v[i].v = vs[k];
        v[i].r = c.r;
        v[i].g = c.g;
        v[i].b = c.b;
        v[i].a = c.a;
    }
}

static int ZZ9K_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                          const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    /* dstrect comes scaled by the render scale (SDL_RenderCopyF) */
    ZZ9K_Vertex *v = (ZZ9K_Vertex *)SDL_AllocateRenderVertices(renderer, 6 * sizeof(ZZ9K_Vertex), 0, &cmd->data.draw.first);
    SDL_Color c;

    if (!v) {
        return -1;
    }
    c.r = cmd->data.draw.r;
    c.g = cmd->data.draw.g;
    c.b = cmd->data.draw.b;
    c.a = cmd->data.draw.a;
    cmd->data.draw.count = 6;
    zz9k_quad(v, dstrect->x, dstrect->y, dstrect->x + dstrect->w, dstrect->y + dstrect->h,
         (float)srcrect->x / texture->w, (float)srcrect->y / texture->h,
         (float)(srcrect->x + srcrect->w) / texture->w, (float)(srcrect->y + srcrect->h) / texture->h, c);
    return 0;
}

static int ZZ9K_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                            const SDL_Rect *srcrect, const SDL_FRect *dstrect,
                            const double angle, const SDL_FPoint *center, const SDL_RendererFlip flip,
                            float scale_x, float scale_y)
{
    ZZ9K_Vertex *v = (ZZ9K_Vertex *)SDL_AllocateRenderVertices(renderer, 6 * sizeof(ZZ9K_Vertex), 0, &cmd->data.draw.first);
    float u0 = (float)srcrect->x / texture->w, v0 = (float)srcrect->y / texture->h;
    float u1 = (float)(srcrect->x + srcrect->w) / texture->w, v1 = (float)(srcrect->y + srcrect->h) / texture->h;
    float rad = (float)(angle * M_PI / 180.0), s = SDL_sinf(rad), co = SDL_cosf(rad);
    float cx = dstrect->x + center->x, cy = dstrect->y + center->y;
    SDL_Color c;
    int i;

    if (!v) {
        return -1;
    }
    if (flip & SDL_FLIP_HORIZONTAL) {
        float t = u0;
        u0 = u1;
        u1 = t;
    }
    if (flip & SDL_FLIP_VERTICAL) {
        float t = v0;
        v0 = v1;
        v1 = t;
    }
    c.r = cmd->data.draw.r;
    c.g = cmd->data.draw.g;
    c.b = cmd->data.draw.b;
    c.a = cmd->data.draw.a;
    cmd->data.draw.count = 6;
    zz9k_quad(v, dstrect->x, dstrect->y, dstrect->x + dstrect->w, dstrect->y + dstrect->h, u0, v0, u1, v1, c);
    for (i = 0; i < 6; i++) {           /* about the centre, then scaled */
        float dx = v[i].x - cx, dy = v[i].y - cy;

        v[i].x = (cx + dx * co - dy * s) * scale_x;
        v[i].y = (cy + dx * s + dy * co) * scale_y;
    }
    return 0;
}

static int ZZ9K_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                              const float *xy, int xy_stride, const SDL_Color *color, int color_stride,
                              const float *uv, int uv_stride, int num_vertices, const void *indices,
                              int num_indices, int size_indices, float scale_x, float scale_y)
{
    int i, count = indices ? num_indices : num_vertices;
    ZZ9K_Vertex *v = (ZZ9K_Vertex *)SDL_AllocateRenderVertices(renderer, count * sizeof(ZZ9K_Vertex), 0, &cmd->data.draw.first);

    if (!v) {
        return -1;
    }
    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;
    for (i = 0; i < count; i++, v++) {
        int j;
        const float *p;
        SDL_Color c;

        if (size_indices == 4) {
            j = ((const Uint32 *)indices)[i];
        } else if (size_indices == 2) {
            j = ((const Uint16 *)indices)[i];
        } else if (size_indices == 1) {
            j = ((const Uint8 *)indices)[i];
        } else {
            j = i;
        }
        p = (const float *)((const char *)xy + j * xy_stride);
        c = *(const SDL_Color *)((const char *)color + j * color_stride);
        v->x = p[0] * scale_x;
        v->y = p[1] * scale_y;
        if (texture) {
            p = (const float *)((const char *)uv + j * uv_stride);
            v->u = p[0];
            v->v = p[1];
        } else {
            v->u = v->v = 0.0f;
        }
        v->r = c.r;
        v->g = c.g;
        v->b = c.b;
        v->a = c.a;
    }
    return 0;
}

/* --- running the queue ---------------------------------------------------- */

/* the rectangle drawing is limited to, in window coordinates */
typedef struct
{
    float x0, y0, x1, y1;
} ZZ9K_Clip;

static void clip_of(const ZZ9K_RenderData *rd, ZZ9K_Clip *c)
{
    int x0 = rd->viewport.x, y0 = rd->viewport.y;
    int x1 = x0 + rd->viewport.w, y1 = y0 + rd->viewport.h;

    if (rd->clipping) {
        x0 = SDL_max(x0, rd->viewport.x + rd->cliprect.x);
        y0 = SDL_max(y0, rd->viewport.y + rd->cliprect.y);
        x1 = SDL_min(x1, rd->viewport.x + rd->cliprect.x + rd->cliprect.w);
        y1 = SDL_min(y1, rd->viewport.y + rd->cliprect.y + rd->cliprect.h);
    }
    c->x0 = (float)SDL_max(x0, 0);
    c->y0 = (float)SDL_max(y0, 0);
    c->x1 = (float)SDL_min(x1, rd->w);
    c->y1 = (float)SDL_min(y1, rd->h);
}

/* a vertex for clipping: everything interpolated as floats */
typedef struct
{
    float x, y, u, v, r, g, b, a;
} CV;

static int clip_edge(const CV *in, int n, CV *out, int edge, float lim)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const CV *p = &in[i], *q = &in[(i + 1) % n];
        float dp = (edge & 1 ? p->y : p->x) - lim, dq = (edge & 1 ? q->y : q->x) - lim;
        SDL_bool pin, qin;

        if (edge >= 2) {            /* upper limits: inside is below the limit */
            dp = -dp;
            dq = -dq;
        }
        pin = dp >= 0.0f;
        qin = dq >= 0.0f;
        if (pin) {
            out[m++] = *p;
        }
        if (pin != qin) {
            float t = dp / (dp - dq);
            CV *o = &out[m++];

            o->x = p->x + (q->x - p->x) * t;
            o->y = p->y + (q->y - p->y) * t;
            o->u = p->u + (q->u - p->u) * t;
            o->v = p->v + (q->v - p->v) * t;
            o->r = p->r + (q->r - p->r) * t;
            o->g = p->g + (q->g - p->g) * t;
            o->b = p->b + (q->b - p->b) * t;
            o->a = p->a + (q->a - p->a) * t;
        }
    }
    return m;
}

static void to_s3d(struct s3d_vertex *s, const CV *c)
{
    s->x = c->x;
    s->y = c->y;
    s->z = 0.5f;
    s->w = 1.0f;
    s->u = c->u;
    s->v = c->v;
    s->r = (unsigned char)(c->r + 0.5f);
    s->g = (unsigned char)(c->g + 0.5f);
    s->b = (unsigned char)(c->b + 0.5f);
    s->a = (unsigned char)(c->a + 0.5f);
}

static void draw_tri(const ZZ9K_Vertex *t, float ox, float oy, const ZZ9K_Clip *c)
{
    CV a[9], b[9];
    struct s3d_vertex s0, s1, s2;
    int i, n = 3;

    for (i = 0; i < 3; i++) {
        a[i].x = t[i].x + ox;
        a[i].y = t[i].y + oy;
        a[i].u = t[i].u;
        a[i].v = t[i].v;
        a[i].r = t[i].r;
        a[i].g = t[i].g;
        a[i].b = t[i].b;
        a[i].a = t[i].a;
    }
    /* the whole triangle inside: as it is */
    for (i = 0; i < 3; i++) {
        if (a[i].x < c->x0 || a[i].x > c->x1 || a[i].y < c->y0 || a[i].y > c->y1) {
            break;
        }
    }
    if (i < 3) {
        n = clip_edge(a, n, b, 0, c->x0);
        n = clip_edge(b, n, a, 1, c->y0);
        n = clip_edge(a, n, b, 2, c->x1);
        n = clip_edge(b, n, a, 3, c->y1);
    }
    if (n < 3) {
        return;
    }
    to_s3d(&s0, &a[0]);
    for (i = 1; i + 1 < n; i++) {       /* a fan */
        to_s3d(&s1, &a[i]);
        to_s3d(&s2, &a[i + 1]);
        s3d_tri_clipped(&s0, &s1, &s2);
    }
}

static int blend_of(SDL_BlendMode mode)
{
    /* GL's numbers, which s3d_gl_factor takes */
    switch (mode) {
    case SDL_BLENDMODE_BLEND:
        return S3D_BLEND(s3d_gl_factor(0x302), s3d_gl_factor(0x303));  /* SRC_ALPHA, 1-SRC_ALPHA */
    case SDL_BLENDMODE_ADD:
        return S3D_BLEND(s3d_gl_factor(0x302), s3d_gl_factor(1));      /* SRC_ALPHA, ONE */
    case SDL_BLENDMODE_MOD:
        return S3D_BLEND(s3d_gl_factor(0), s3d_gl_factor(0x300));      /* ZERO, SRC_COLOR */
    case SDL_BLENDMODE_MUL:
        return S3D_BLEND(s3d_gl_factor(0x306), s3d_gl_factor(0x303));  /* DST_COLOR, 1-SRC_ALPHA */
    default:
        return S3D_REPLACE;
    }
}

static void set_state(SDL_Texture *texture, SDL_BlendMode blend, SDL_bool white)
{
    struct s3d_state st;

    SDL_zero(st);
    st.tex = texture ? ((ZZ9K_TextureData *)texture->driverdata)->tex : NULL;
    st.blend = blend_of(blend);
    st.zmode = S3D_ZMODE(0, S3D_Z_ALWAYS);
    st.texenv = texture && white ? S3D_ENV_REPLACE : S3D_ENV_MODULATE;
    s3d_set_state(&st);
}

static Uint16 rgb565(Uint8 r, Uint8 g, Uint8 b)
{
    return (Uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* an opaque rectangle (window coordinates) by the blitter, clipped */
static void fill_rect(float x0, float y0, float x1, float y1, const ZZ9K_Clip *c, Uint16 color)
{
    int ix0, iy0, ix1, iy1;

    x0 = SDL_max(x0, c->x0);
    y0 = SDL_max(y0, c->y0);
    x1 = SDL_min(x1, c->x1);
    y1 = SDL_min(y1, c->y1);
    ix0 = (int)x0;
    iy0 = (int)y0;
    ix1 = (int)x1;
    iy1 = (int)y1;
    if (ix1 > ix0 && iy1 > iy0) {
        s3d_clear_color(ix0, iy0, ix1 - ix0, iy1 - iy0, color);
    }
}

static SDL_bool opaque(SDL_BlendMode blend, Uint8 a)
{
    return blend == SDL_BLENDMODE_NONE || (a == 255 && (blend == SDL_BLENDMODE_BLEND));
}

static void draw_line(const SDL_FPoint *p, const SDL_FPoint *q, float ox, float oy, const ZZ9K_Clip *c,
                      const SDL_RenderCommand *cmd)
{
    float dx = q->x - p->x, dy = q->y - p->y, len, nx, ny;
    ZZ9K_Vertex v[6];
    SDL_Color col;

    col.r = cmd->data.draw.r;
    col.g = cmd->data.draw.g;
    col.b = cmd->data.draw.b;
    col.a = cmd->data.draw.a;
    if (opaque(cmd->data.draw.blend, col.a) && (dx == 0.0f || dy == 0.0f)) {
        float x0 = SDL_min(p->x, q->x), y0 = SDL_min(p->y, q->y);

        fill_rect(x0 + ox, y0 + oy, x0 + ox + SDL_fabsf(dx) + 1, y0 + oy + SDL_fabsf(dy) + 1, c,
                  rgb565(col.r, col.g, col.b));
        return;
    }
    len = SDL_sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }
    nx = -dy / len * 0.5f;              /* half a pixel either side */
    ny = dx / len * 0.5f;
    zz9k_quad(v, 0, 0, 0, 0, 0, 0, 0, 0, col);
    v[0].x = v[3].x = p->x + 0.5f + nx; v[0].y = v[3].y = p->y + 0.5f + ny;
    v[1].x = q->x + 0.5f + nx;          v[1].y = q->y + 0.5f + ny;
    v[2].x = v[4].x = q->x + 0.5f - nx; v[2].y = v[4].y = q->y + 0.5f - ny;
    v[5].x = p->x + 0.5f - nx;          v[5].y = p->y + 0.5f - ny;
    draw_tri(&v[0], ox, oy, c);
    draw_tri(&v[3], ox, oy, c);
}

static int ZZ9K_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    ZZ9K_RenderData *rd = (ZZ9K_RenderData *)renderer->driverdata;
    ZZ9K_Clip clip;

    clip_of(rd, &clip);
    for (; cmd; cmd = cmd->next) {
        float ox = (float)rd->viewport.x, oy = (float)rd->viewport.y;

        switch (cmd->command) {
        case SDL_RENDERCMD_SETDRAWCOLOR:
            break;
        case SDL_RENDERCMD_SETVIEWPORT:
            rd->viewport = cmd->data.viewport.rect;
            clip_of(rd, &clip);
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            rd->clipping = cmd->data.cliprect.enabled;
            rd->cliprect = cmd->data.cliprect.rect;
            clip_of(rd, &clip);
            break;
        case SDL_RENDERCMD_CLEAR:
            /* all of it: a clear ignores the viewport and the clip rectangle */
            s3d_clear_color(0, 0, rd->w, rd->h,
                            rgb565(cmd->data.color.r, cmd->data.color.g, cmd->data.color.b));
            rd->dirty = SDL_TRUE;
            break;
        case SDL_RENDERCMD_DRAW_POINTS: {
            const SDL_FPoint *p = (const SDL_FPoint *)((Uint8 *)vertices + cmd->data.draw.first);
            SDL_Color col;
            size_t i;

            col.r = cmd->data.draw.r;
            col.g = cmd->data.draw.g;
            col.b = cmd->data.draw.b;
            col.a = cmd->data.draw.a;
            if (opaque(cmd->data.draw.blend, col.a)) {
                Uint16 c565 = rgb565(col.r, col.g, col.b);

                for (i = 0; i < cmd->data.draw.count; i++) {
                    fill_rect(p[i].x + ox, p[i].y + oy, p[i].x + ox + 1, p[i].y + oy + 1, &clip, c565);
                }
            } else {
                ZZ9K_Vertex v[6];

                set_state(NULL, cmd->data.draw.blend, SDL_FALSE);
                for (i = 0; i < cmd->data.draw.count; i++) {
                    zz9k_quad(v, p[i].x, p[i].y, p[i].x + 1, p[i].y + 1, 0, 0, 0, 0, col);
                    draw_tri(&v[0], ox, oy, &clip);
                    draw_tri(&v[3], ox, oy, &clip);
                }
            }
            rd->dirty = SDL_TRUE;
            break;
        }
        case SDL_RENDERCMD_DRAW_LINES: {
            const SDL_FPoint *p = (const SDL_FPoint *)((Uint8 *)vertices + cmd->data.draw.first);
            size_t i;

            set_state(NULL, opaque(cmd->data.draw.blend, cmd->data.draw.a) ?
                      SDL_BLENDMODE_NONE : cmd->data.draw.blend, SDL_FALSE);
            for (i = 0; i + 1 < cmd->data.draw.count; i++) {
                draw_line(&p[i], &p[i + 1], ox, oy, &clip, cmd);
            }
            rd->dirty = SDL_TRUE;
            break;
        }
        case SDL_RENDERCMD_FILL_RECTS: {
            const SDL_FRect *r = (const SDL_FRect *)((Uint8 *)vertices + cmd->data.draw.first);
            SDL_Color col;
            size_t i;

            col.r = cmd->data.draw.r;
            col.g = cmd->data.draw.g;
            col.b = cmd->data.draw.b;
            col.a = cmd->data.draw.a;
            if (opaque(cmd->data.draw.blend, col.a)) {
                Uint16 c565 = rgb565(col.r, col.g, col.b);

                for (i = 0; i < cmd->data.draw.count; i++) {
                    fill_rect(r[i].x + ox, r[i].y + oy, r[i].x + r[i].w + ox, r[i].y + r[i].h + oy, &clip, c565);
                }
            } else {
                ZZ9K_Vertex v[6];

                set_state(NULL, cmd->data.draw.blend, SDL_FALSE);
                for (i = 0; i < cmd->data.draw.count; i++) {
                    zz9k_quad(v, r[i].x, r[i].y, r[i].x + r[i].w, r[i].y + r[i].h, 0, 0, 0, 0, col);
                    draw_tri(&v[0], ox, oy, &clip);
                    draw_tri(&v[3], ox, oy, &clip);
                }
            }
            rd->dirty = SDL_TRUE;
            break;
        }
        case SDL_RENDERCMD_COPY:
        case SDL_RENDERCMD_COPY_EX:
        case SDL_RENDERCMD_GEOMETRY: {
            const ZZ9K_Vertex *v = (const ZZ9K_Vertex *)((Uint8 *)vertices + cmd->data.draw.first);
            SDL_Texture *texture = cmd->data.draw.texture;
            SDL_BlendMode blend = cmd->data.draw.blend;
            SDL_bool white = SDL_TRUE;
            size_t i;

            if (texture) {
                /* the texture's own colour and alpha mod come with the vertices */
                if (cmd->command != SDL_RENDERCMD_GEOMETRY) {
                    white = cmd->data.draw.r == 255 && cmd->data.draw.g == 255 &&
                            cmd->data.draw.b == 255 && cmd->data.draw.a == 255;
                } else {
                    for (i = 0; i < cmd->data.draw.count && white; i++) {
                        white = v[i].r == 255 && v[i].g == 255 && v[i].b == 255 && v[i].a == 255;
                    }
                }
            }
            set_state(texture, blend, white);
            for (i = 0; i + 3 <= cmd->data.draw.count; i += 3) {
                draw_tri(&v[i], ox, oy, &clip);
            }
            rd->dirty = SDL_TRUE;
            break;
        }
        case SDL_RENDERCMD_NO_OP:
            break;
        }
    }
    return 0;
}

static int ZZ9K_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch)
{
    ZZ9K_RenderData *rd = (ZZ9K_RenderData *)renderer->driverdata;
    unsigned short *back;
    int stride, x = rd->viewport.x + rect->x, y = rd->viewport.y + rect->y;

    s3d_finish();
    back = s3d_backbuffer(&stride);
    if (!back) {
        return SDL_SetError("zz9k: back buffer not mapped");
    }
    if (x < 0 || y < 0 || x + rect->w > rd->w || y + rect->h > rd->h) {
        return SDL_SetError("zz9k: rectangle outside the window");
    }
    return SDL_ConvertPixels(rect->w, rect->h, SDL_PIXELFORMAT_RGB565,
                             back + (size_t)y * stride + x, stride * 2, format, pixels, pitch);
}

static int ZZ9K_RenderPresent(SDL_Renderer *renderer)
{
    ZZ9K_RenderData *rd = (ZZ9K_RenderData *)renderer->driverdata;

    s3d_swap(rd->display, rd->xwindow);
    rd->dirty = SDL_FALSE;
    return 0;
}

static int ZZ9K_Resize(ZZ9K_RenderData *rd, int w, int h)
{
    if (w == rd->w && h == rd->h) {
        return 0;
    }
    if (s3d_resize(w, h) != 0) {
        return SDL_SetError("zz9k: soft3D cannot draw %dx%d", w, h);
    }
    rd->w = w;
    rd->h = h;
    s3d_clear_color(0, 0, w, h, 0);
    return 0;
}

static void ZZ9K_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    ZZ9K_RenderData *rd = (ZZ9K_RenderData *)renderer->driverdata;

    if (event->event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        int w, h;

        SDL_GetWindowSizeInPixels(renderer->window, &w, &h);
        ZZ9K_Resize(rd, w, h);
    }
}

static int ZZ9K_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    ZZ9K_RenderData *rd = (ZZ9K_RenderData *)renderer->driverdata;

    if (w) {
        *w = rd->w;
    }
    if (h) {
        *h = rd->h;
    }
    return 0;
}

static void ZZ9K_DestroyRenderer(SDL_Renderer *renderer)
{
    SDL_free(renderer->driverdata);
    SDL_free(renderer);
}

static SDL_Renderer *ZZ9K_CreateRenderer(SDL_Window *window, Uint32 flags)
{
    SDL_SysWMinfo info;
    SDL_Renderer *renderer;
    ZZ9K_RenderData *rd;
    int w, h;

    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_X11) {
        SDL_SetError("zz9k: needs an X11 window");
        return NULL;
    }
    s3d_verbose(SDL_getenv("SDL_ZZ9K_VERBOSE") != NULL);
    if (s3d_init(info.info.x11.display, "SDL zz9k") != 0) {
        SDL_SetError("zz9k: soft3D not available");
        return NULL;
    }
    renderer = (SDL_Renderer *)SDL_calloc(1, sizeof(*renderer));
    rd = (ZZ9K_RenderData *)SDL_calloc(1, sizeof(*rd));
    if (!renderer || !rd) {
        SDL_free(renderer);
        SDL_free(rd);
        SDL_OutOfMemory();
        return NULL;
    }
    rd->display = info.info.x11.display;
    rd->xwindow = info.info.x11.window;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    rd->viewport.w = w;
    rd->viewport.h = h;
    if (ZZ9K_Resize(rd, w, h) != 0) {
        SDL_free(renderer);
        SDL_free(rd);
        return NULL;
    }

    renderer->WindowEvent = ZZ9K_WindowEvent;
    renderer->GetOutputSize = ZZ9K_GetOutputSize;
    renderer->SupportsBlendMode = ZZ9K_SupportsBlendMode;
    renderer->CreateTexture = ZZ9K_CreateTexture;
    renderer->UpdateTexture = ZZ9K_UpdateTexture;
#if SDL_HAVE_YUV
    renderer->UpdateTextureYUV = ZZ9K_UpdateTextureYUV;
#endif
    renderer->LockTexture = ZZ9K_LockTexture;
    renderer->UnlockTexture = ZZ9K_UnlockTexture;
    renderer->SetTextureScaleMode = ZZ9K_SetTextureScaleMode;
    renderer->SetRenderTarget = ZZ9K_SetRenderTarget;
    renderer->QueueSetViewport = ZZ9K_QueueNoop;
    renderer->QueueSetDrawColor = ZZ9K_QueueNoop;
    renderer->QueueDrawPoints = ZZ9K_QueueDrawPoints;
    renderer->QueueDrawLines = ZZ9K_QueueDrawPoints;     /* the same points, joined when run */
    renderer->QueueFillRects = ZZ9K_QueueFillRects;
    renderer->QueueCopy = ZZ9K_QueueCopy;
    renderer->QueueCopyEx = ZZ9K_QueueCopyEx;
    renderer->QueueGeometry = ZZ9K_QueueGeometry;
    renderer->RunCommandQueue = ZZ9K_RunCommandQueue;
    renderer->RenderReadPixels = ZZ9K_RenderReadPixels;
    renderer->RenderPresent = ZZ9K_RenderPresent;
    renderer->DestroyTexture = ZZ9K_DestroyTexture;
    renderer->DestroyRenderer = ZZ9K_DestroyRenderer;
    renderer->info = ZZ9K_RenderDriver.info;
    renderer->driverdata = rd;
    return renderer;
}

SDL_RenderDriver ZZ9K_RenderDriver = {
    ZZ9K_CreateRenderer,
    {
     "zz9k",
     SDL_RENDERER_ACCELERATED,
     4,
     {
      SDL_PIXELFORMAT_RGBA8888,
      SDL_PIXELFORMAT_RGB24,
      SDL_PIXELFORMAT_IYUV,
      SDL_PIXELFORMAT_YV12
     },
     ZZ9K_MAX_TEXTURE,
     ZZ9K_MAX_TEXTURE }
};

#endif /* SDL_VIDEO_RENDER_ZZ9K */
