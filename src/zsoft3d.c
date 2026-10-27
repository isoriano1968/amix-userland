/*
 * zsoft3d.c -- TinyGL's rasterizer on the Z3660's ARM (AMIX, 2026-10-26).
 *
 * The Z3660 firmware runs Wazp3D's software rasterizer ("soft3D") on its
 * ARM.  Unless TINYGL_SOFT3D=0 is in the environment, TinyGL keeps transform,
 * lighting and clipping on the 68060 and hands every filled triangle,
 * projected, to soft3D in batches; soft3D draws them into a back buffer in
 * off-screen VRAM, and glXSwapBuffers has the card's blitter copy that into
 * the window.  The window is drawn over directly: anything overlapping it
 * is painted over (fine for demos and full-screen programs).
 *
 * The protocol (shanshe/Z3660 firmware, wazp3d/soft3d56.c): parameters in a
 * block at board+0x4200000, the operation in REG_ZZ_SOFT3D_OP, every pointer
 * a 68k board address (the ARM maps the board there too), big-endian
 * structures.  Needs a kernel whose zz9000 driver maps the whole board and
 * has ZZ9KIOC_SOFT3D and ZZ9KIOC_BLITX (-DZZ9K_MAP_BOARD).
 *
 * Textures: TinyGL's 256x256 RGB texels are copied into board memory on
 * first use and registered with CreateTexture; soft3D samples them there.
 *
 * Not yet: lines and points (they still go to TinyGL's own buffer, which
 * is not shown in this mode), blending.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <utmp.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <X11/Xlib.h>
#include "zgl.h"
#include "zsoft3d.h"

/* the AMIX zz9000 driver (zz9000.h) */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_GETFBINFO   (ZZ9K_IOC | 11)
#define ZZ9KIOC_SOFT3D      (ZZ9K_IOC | 17)
#define ZZ9KIOC_BLITX       (ZZ9K_IOC | 18)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_fbinfo {
    unsigned long fb_offset, fb_size;
    unsigned short width, height, color_mode, depth, bytes_per_pixel, stride;
    unsigned long red_mask, green_mask, blue_mask, alpha_mask;
    unsigned short red_shift, green_shift, blue_shift, alpha_shift;
};
struct zz9k_soft3d { unsigned long op, result; };
struct zz9k_blitx {
    unsigned short op, sx, sy, dx, dy, width, height, pad;
    unsigned long color;
};

/* the firmware */
enum { OP_SETBITMAP, OP_SETCLIPPING, OP_SETDRAWSTATE, OP_DRAWPRIMITIVE, OP_DOUPDATE,
       OP_FLUSH, OP_END, OP_CREATETEXTURE, OP_FREETEXTURE, OP_UPDATETEXTURE, OP_START,
       OP_ALLOCZBUFFER, OP_ALLOCIMAGEBUFFER, OP_CLEARZBUFFER };
#define FB_PHYS             0x00200000UL    /* device offset 0 = board + this */
#define SOFT3D_DATA         0x04200000UL    /* the parameter block, board offset */
#define PIXFMT_RGB16        5
#define W3D_ZERO            1
#define W3D_ONE             2
#define W3D_Z_LESS          2
#define W3D_PRIMITIVE_TRIANGLES 0
#define W3D_R8G8B8          4               /* 24-bit texels */
#define W3D_R8G8B8A8        11              /* 32-bit texels */
#define W3D_REPLACE         1               /* the GL texture environments */
#define W3D_DECAL           2
#define W3D_MODULATE        3
#define W3D_BLEND           4
#define W3D_Z_ALWAYS        8
#define PERSP_CORRECT       1               /* 0 affine, 1 per pixel, 2 every 2x2 */
#define W3D_NOW             255
#define ZMODE(upd, cmp)     ((upd) * 8 + ((cmp) - 1))
#define BLENDREPLACE        (W3D_ONE * 16 + W3D_ZERO)
#define BUTTON_SIZE         136
#define BTN_RENDERER        9
#define PREFS_SIZE          7760
#define MAXPOINTS           2997            /* firmware MAXPRIM 3000, whole triangles */

struct soft3d_data {
    unsigned long offset[4];
    unsigned long format[2];
    unsigned short x[2], y[2];
};
struct state3D {
    unsigned char Changed, ZMode, BlendMode, TexEnvMode;
    unsigned char PerspMode, CullingMode, FogMode, UseGouraud;
    unsigned char UseTex, UseFog, pad3, pad4;
    unsigned char CurrentRGBA[4], EnvRGBA[4], FogRGBA[4], BackRGBA[4];
    unsigned long PointSize, LineSize, primitive;
    float FogZmin, FogZmax, FogDensity;
    unsigned long gltex, ST, ST2;
};
struct point3D {
    float x, y, z, u, v, w;
    unsigned char RGBA[4], pad[4];
};

/* where things live in the device space (device 0 = board + FB_PHYS) */
#define BACK_ROW            1100            /* back buffer: off-screen rows */
#define DATA_OFF            0x2400000UL     /* prefs, state, vertices: 36 MB in */
#define DATA_SIZE           0x40000UL
#define TEX_OFF             0x2500000UL     /* textures: 37 MB in, 8 MB */
#define TEX_SIZE            0x800000UL
#define TEX_BYTES           (256 * 256 * 3)

int s3d_active;

static int fd = -1;
static volatile struct soft3d_data *blk;
static char *data;
static unsigned char *prefs;
static struct state3D *state;
/* the batch is built here, in cached memory, and sent to the board in one
   burst at its flush: built in place on the board, every field was a
   separate Zorro write and every check a Zorro read */
static struct point3D batch[MAXPOINTS + 3] __attribute__((aligned(16)));
static struct point3D *bbatch;                  /* its place on the board */
static unsigned long a_prefs, a_state, a_batch, board, sc;
/* The firmware's soft3D heap is 16 MB on the board, and SOFT3D_End frees
   the context but not its Z buffer or textures: a program that does not
   give them back leaks them until the Amiga is reset, and once the heap is
   full an ALLOCZBUFFER returns NULL and the next draw takes the ARM (and
   the 68k with it) down.  So: textures are freed and the Z buffer shrunk to
   1x1 before END, at exit and on the usual signals; and what a process that
   died without that left is recorded here, in board memory, for the next
   one to free.  One soft3D user at a time (the data block is shared). */
#define OWNER_OFF           0x38000         /* in the data block; libs3d's too */
#define OWNER_MAGIC         0x53334f57UL    /* "S3OW" */
#define MAXTEX              4096            /* the record libs3d (Mesa) shares */
struct owner {
    unsigned long magic;
    long boot, pid;
    unsigned long sc, ntex;
    unsigned long tex[MAXTEX];
};
static volatile struct owner *own;

static int nbatch, width, height, screen_w, screen_h, stride_px, fb_rows;
static char *texmem;
static unsigned long texused;
/* the draw state soft3D has: texture, blend, Z and texenv modes */
struct dstate { int usetex; unsigned long st; int blend, zmode, texenv; };
static struct dstate cur;
static int nov;                                 /* blended triangles in the fragment buffer */
static void flush_batch(void);

/* TINYGL_S3D_CHECK=1: a dry run -- every triangle is built and checked but
   nothing is drawn by soft3D; the rejects are counted and reported */
static int check;
static unsigned long n_tri, n_bad, n_degen, n_frames;
static float lo[4], hi[4];

#define AMIGA(ptr) (board + FB_PHYS + DATA_OFF + (unsigned long)((char *)(ptr) - data))

/* TINYGL_S3D_PROF=1: where a frame's time goes, every 100 frames */
#include <sys/time.h>
extern int gettimeofday();
static int prof;
static int noswap, noclear;     /* experiments: TINYGL_S3D_NOSWAP/NOCLEAR/NOCOPY=1 */
static int nocopy;
static double pt_op[16], pt_copy, pt_swap, pt_frame;
static unsigned long pn_op[16], pn_frames;
static struct timeval pt_last;

static double now_us(void)
{
    struct timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * 1e6 + t.tv_usec;
}

static void prof_report(void)
{
    static const char *name[16] = { "setbitmap", "setclip", "state", "draw", "update",
        "flush", "end", "createtex", "freetex", "updtex", "start", "allocz", "allocimg", "clearz" };
    int i;

    fprintf(stderr, "s3d prof, ms per frame over %lu frames: frame %.2f, copy %.2f, swap %.2f;",
            pn_frames, pt_frame / pn_frames / 1000, pt_copy / pn_frames / 1000, pt_swap / pn_frames / 1000);
    for (i = 0; i < 16; i++)
        if (pn_op[i])
            fprintf(stderr, " %s %.2f (%.1f/frame)", name[i], pt_op[i] / pn_frames / 1000,
                    (double) pn_op[i] / pn_frames);
    fprintf(stderr, "\n");
    memset(pt_op, 0, sizeof pt_op);
    memset(pn_op, 0, sizeof pn_op);
    pt_copy = pt_swap = pt_frame = 0;
    pn_frames = 0;
}

static unsigned long op(int code)
{
    struct zz9k_soft3d s;
    double t0 = 0;
    if (prof) {
        t0 = now_us();
        pn_op[code & 15]++;
    }
    if (check && (code == OP_DRAWPRIMITIVE || code == OP_FLUSH || code == OP_DOUPDATE ||
                  code == OP_CLEARZBUFFER || code == OP_CREATETEXTURE || code == OP_UPDATETEXTURE))
        return code == OP_CREATETEXTURE ? 1 : 0;
    s.op = code;
    s.result = 0;
    if (ioctl(fd, ZZ9KIOC_SOFT3D, &s) < 0)
        s.result = 0;
    if (prof)
        pt_op[code & 15] += now_us() - t0;
    return s.result;
}

static int blitx(int o, int sx, int sy, int dx, int dy, int w, int h, unsigned long color)
{
    struct zz9k_blitx b;
    b.op = o; b.sx = sx; b.sy = sy; b.dx = dx; b.dy = dy;
    b.width = w; b.height = h; b.pad = 0; b.color = color;
    return ioctl(fd, ZZ9KIOC_BLITX, &b);
}

/* give an soft3D context and everything recorded with it back */
static void release(unsigned long ctx, volatile struct owner *o)
{
    unsigned long i;

    for (i = 0; i < o->ntex && i < MAXTEX; i++)
        if (o->tex[i]) {
            blk->offset[0] = ctx; blk->offset[1] = o->tex[i];
            op(OP_FREETEXTURE);
        }
    blk->offset[0] = ctx; blk->x[0] = 1; blk->y[0] = 1;
    op(OP_ALLOCZBUFFER);                        /* frees the big one */
    blk->offset[0] = ctx;
    op(OP_END);
    o->ntex = 0;
    o->magic = 0;
}

/* when the system came up: a record from before a reboot is not ours to free */
static long boot_time(void)
{
    struct utmp u, *b;
    long t = 0;

    setutent();
    u.ut_type = BOOT_TIME;
    if ((b = getutid(&u)) != NULL)
        t = (long) b->ut_time;
    endutent();
    return t;
}

static void s3d_end(void)
{
    if (fd < 0 || !sc)
        return;
    if (own->magic == OWNER_MAGIC && own->pid == (long) getpid())
        release(sc, own);
    sc = 0;
    s3d_active = 0;
}

static void on_signal(int sig)
{
    s3d_end();
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

/* messages about soft3D not being there only when TINYGL_SOFT3D=1 asked for
   it; by default the program just draws on the 68k */
static int verbose;

static int s3d_open(void)
{
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    char *p;

    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0)
        return -1;
    if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0 || ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi) < 0)
        goto fail;
    board = info.base;
    screen_w = fbi.width;
    screen_h = fbi.height;
    stride_px = fbi.stride / 2;
    fb_rows = fbi.fb_size / fbi.stride;
    data = (char *) mmap(0, DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) DATA_OFF);
    if (data == (char *) -1)
        goto fail;
    texmem = (char *) mmap(0, TEX_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) TEX_OFF);
    if (texmem == (char *) -1)
        goto fail;
    p = (char *) mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                      (off_t) (SOFT3D_DATA - FB_PHYS));
    if (p == (char *) -1)
        goto fail;
    blk = (volatile struct soft3d_data *) p;
    own = (volatile struct owner *) (data + OWNER_OFF);
    if (own->magic == OWNER_MAGIC && own->boot == boot_time() && own->pid != (long) getpid()) {
        if (kill((pid_t) own->pid, 0) == 0 || errno == EPERM) {
            fprintf(stderr, "TinyGL: soft3D is in use by process %ld\n", own->pid);
            goto fail;
        }
        release(own->sc, own);                  /* a dead owner's leftovers */
    }
    prefs = (unsigned char *) data;
    state = (struct state3D *) (data + 0x2000);
    bbatch = (struct point3D *) (data + 0x4000);
    a_prefs = AMIGA(prefs); a_state = AMIGA(state); a_batch = AMIGA(bbatch);

    memset(prefs, 0, PREFS_SIZE);
    prefs[BTN_RENDERER * BUTTON_SIZE + 1] = 1;      /* soft to bitmap */
    blk->offset[0] = a_prefs;
    sc = op(OP_START);
    if (!sc)
        goto fail;
    own->sc = sc;
    own->ntex = 0;
    own->pid = (long) getpid();
    own->boot = boot_time();
    own->magic = OWNER_MAGIC;
    atexit(s3d_end);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    return 0;
fail:
    if (verbose)
        fprintf(stderr, "TinyGL: soft3D not available (needs the Z3660 and a ZZ9K_MAP_BOARD kernel)\n");
    close(fd);
    fd = -1;
    return -1;
}

/* the draw state: Gouraud, Z less, replace; textured with st if usetex */
static void set_state(const struct dstate *d)
{
    memset(state, 0, sizeof *state);
    state->Changed = 1;
    state->ZMode = d->zmode;
    state->BlendMode = d->blend;
    state->UseGouraud = 1;
    state->CullingMode = W3D_NOW;                   /* TinyGL culls */
    state->UseTex = d->usetex;
    state->TexEnvMode = d->texenv;
    state->PerspMode = PERSP_CORRECT;
    state->CurrentRGBA[0] = state->CurrentRGBA[1] = state->CurrentRGBA[2] = state->CurrentRGBA[3] = 255;
    state->PointSize = state->LineSize = 1;
    state->primitive = W3D_PRIMITIVE_TRIANGLES;
    state->ST = d->usetex ? d->st : 0;
    blk->offset[0] = sc; blk->offset[1] = a_state;
    op(OP_SETDRAWSTATE);                            /* flushes the fragments */
    nov = 0;
    cur = *d;
    cur.st = state->ST;
}

/* GL blend factor -> Warp3D's (the same names); 0 = not one soft3D has */
static int w3d_factor(int f)
{
    switch (f) {
    case GL_ZERO:                   return W3D_ZERO;
    case GL_ONE:                    return W3D_ONE;
    case GL_SRC_COLOR:              return 3;
    case GL_DST_COLOR:              return 4;
    case GL_ONE_MINUS_SRC_COLOR:    return 5;
    case GL_ONE_MINUS_DST_COLOR:    return 6;
    case GL_SRC_ALPHA:              return 7;
    case GL_ONE_MINUS_SRC_ALPHA:    return 8;
    case GL_DST_ALPHA:              return 9;
    case GL_ONE_MINUS_DST_ALPHA:    return 10;
    case GL_SRC_ALPHA_SATURATE:     return 11;
    }
    return 0;
}

/*
 * The state a primitive wants, from the GL state.  Z: TinyGL's own
 * rasterizer Z-tests every triangle whatever GL_DEPTH_TEST says, and its
 * programs rely on it; so until a program touches GL_DEPTH_TEST soft3D
 * does too, after that it follows GL (lines and points always did).
 * Blending: any GL factor pair.  soft3D has no alpha test: an alpha test
 * without blending becomes SRC_ALPHA/ONE_MINUS_SRC_ALPHA, which looks the
 * same for the usual cut-out textures (but writes Z where they are clear).
 */
static void want_state(GLContext *c, int usetex, unsigned long st, int ztest, struct dstate *d)
{
    int s, t;

    d->usetex = usetex;
    d->st = usetex ? st : 0;
    d->zmode = ztest ? ZMODE(c->depth_mask ? 1 : 0, W3D_Z_LESS) : ZMODE(0, W3D_Z_ALWAYS);
    d->blend = BLENDREPLACE;
    if (c->blend_enabled) {
        s = w3d_factor(c->blend_sfactor);
        t = w3d_factor(c->blend_dfactor);
        if (s && t)
            d->blend = s * 16 + t;
    } else if (c->alpha_test_enabled && c->alpha_func != GL_ALWAYS)
        d->blend = 7 * 16 + 8;
    switch (c->tex_env_mode) {
    case GL_REPLACE:    d->texenv = W3D_REPLACE; break;
    case GL_DECAL:      d->texenv = W3D_DECAL; break;
    case GL_BLEND:      d->texenv = W3D_BLEND; break;
    default:            d->texenv = W3D_MODULATE; break;
    }
}

/*
 * Blending and the fragment buffer.  Wazp3D collects the fragments of
 * every triangle in a buffer and only at its flush reads the destination
 * pixels, blends and writes them -- so two blended triangles in the same
 * buffer that overlap both blend with the old destination, and the later
 * one simply wins.  (A state change flushes; opaque drawing reads nothing.)
 * So while blending, the screen triangles in the buffer are kept, and a
 * triangle that overlaps one of them first sends the batch and flushes.
 * The test is exact (separating axes), so the two halves of a quad, which
 * only share an edge, do not count.
 */
#define MAXOV 48
static float ov[MAXOV][6];

static int separated(const float *a, const float *b)
{
    int i, k;
    const float *t;

    for (k = 0; k < 2; k++) {
        t = k ? b : a;
        for (i = 0; i < 3; i++) {
            float nx = t[((i + 1) % 3) * 2 + 1] - t[i * 2 + 1];
            float ny = t[i * 2] - t[((i + 1) % 3) * 2];
            float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f, d;
            int j;
            for (j = 0; j < 3; j++) {
                d = a[j * 2] * nx + a[j * 2 + 1] * ny;
                if (d < amin) amin = d;
                if (d > amax) amax = d;
                d = b[j * 2] * nx + b[j * 2 + 1] * ny;
                if (d < bmin) bmin = d;
                if (d > bmax) bmax = d;
            }
            d = 0.01f * ((nx < 0 ? -nx : nx) + (ny < 0 ? -ny : ny));
            if (amax <= bmin + d || bmax <= amin + d)
                return 1;
        }
    }
    return 0;
}

static void flush_fragments(void)
{
    flush_batch();
    blk->offset[0] = sc;
    op(OP_FLUSH);
    nov = 0;
}

/* the last triangle in the batch, while blending: flush first if it
   would land on fragments still in the buffer */
static void blend_order(void)
{
    struct point3D *p = &batch[nbatch - 3], keep[3];
    float t[6];
    int i;

    for (i = 0; i < 3; i++) {
        t[i * 2] = p[i].x;
        t[i * 2 + 1] = p[i].y;
    }
    for (i = 0; i < nov; i++)
        if (!separated(t, ov[i]))
            break;
    if (i < nov || nov == MAXOV) {
        memcpy(keep, p, sizeof keep);
        nbatch -= 3;
        flush_fragments();
        memcpy(&batch[0], keep, sizeof keep);
        nbatch = 3;
    }
    memcpy(ov[nov++], t, sizeof t);
}

static int same_state(const struct dstate *a, const struct dstate *b)
{
    return a->usetex == b->usetex && a->st == b->st && a->blend == b->blend &&
           a->zmode == b->zmode && a->texenv == b->texenv;
}

/* the image's texels in board memory, registered with soft3D */
static unsigned long texture(GLImage *im)
{
    unsigned long bytes;

    if (im->s3d_rgb == NULL)
        return 0;
    if (im->s3d_board != 0 && im->s3d_slot != im->s3d_bpp) {
        /* RGB <-> RGBA: a new slot (the old one stays used) and texture */
        if (im->s3d_tex) {
            unsigned long i;
            blk->offset[0] = sc; blk->offset[1] = im->s3d_tex;
            op(OP_FREETEXTURE);
            for (i = 0; i < own->ntex; i++)
                if (own->tex[i] == im->s3d_tex)
                    own->tex[i] = 0;
            im->s3d_tex = 0;
        }
        im->s3d_board = 0;
    }
    if (im->s3d_board == 0) {
        bytes = 256UL * 256 * im->s3d_bpp;
        if (texused + bytes > TEX_SIZE)
            return 0;                               /* out of texture memory */
        im->s3d_board = board + FB_PHYS + TEX_OFF + texused;
        im->s3d_slot = im->s3d_bpp;
        texused += bytes;
    }
    if (im->s3d_dirty) {
        memcpy(texmem + (im->s3d_board - (board + FB_PHYS + TEX_OFF)), im->s3d_rgb,
               256UL * 256 * im->s3d_bpp);
        if (im->s3d_tex == 0) {
            blk->offset[0] = sc; blk->offset[1] = im->s3d_board;
            blk->x[0] = 256; blk->y[0] = 256;
            blk->x[1] = im->s3d_bpp == 4 ? W3D_R8G8B8A8 : W3D_R8G8B8; blk->y[1] = 0;
            im->s3d_tex = op(OP_CREATETEXTURE);
            if (im->s3d_tex && !check && own->ntex < MAXTEX && own->ntex < TEX_SIZE / TEX_BYTES)
                own->tex[own->ntex++] = im->s3d_tex;
        } else {
            blk->offset[0] = sc; blk->offset[1] = im->s3d_tex; blk->offset[2] = im->s3d_board;
            op(OP_UPDATETEXTURE);
        }
        im->s3d_dirty = 0;
    }
    return im->s3d_tex;
}

static void flush_batch(void);

/* (re)size: back buffer bitmap, Z buffer, clipping, draw state */
int s3d_xquery(Display *dpy);
void s3d_xswap(Display *dpy, int major, Window win, int sx, int sy, int w, int h);

/* the X server's ZZ9K-SOFT3D major opcode; -1 = copy straight to the
   screen (TINYGL_SOFT3D_DIRECT=1, or no extension and that was asked for) */
static int xmajor;

int s3d_begin(Display *dpy, int w, int h)
{
    if (fd >= 0 && !sc)
        return -1;                              /* given up on before */
    verbose = getenv("TINYGL_SOFT3D") && atoi(getenv("TINYGL_SOFT3D")) > 0;
    if (xmajor == 0) {
        if (getenv("TINYGL_SOFT3D_DIRECT") && atoi(getenv("TINYGL_SOFT3D_DIRECT")) > 0)
            xmajor = -1;
        else if ((xmajor = s3d_xquery(dpy)) == 0) {
            if (verbose)
                fprintf(stderr, "TinyGL: the X server has no ZZ9K-SOFT3D (an older Xzz9000), "
                    "drawing on the 68k; TINYGL_SOFT3D_DIRECT=1 copies over other windows\n");
            return -1;
        }
    }
    if (fd < 0) {
        check = getenv("TINYGL_S3D_CHECK") && atoi(getenv("TINYGL_S3D_CHECK")) > 0;
        prof = getenv("TINYGL_S3D_PROF") && atoi(getenv("TINYGL_S3D_PROF")) > 0;
        noswap = getenv("TINYGL_S3D_NOSWAP") != NULL;
        noclear = getenv("TINYGL_S3D_NOCLEAR") != NULL;
        nocopy = getenv("TINYGL_S3D_NOCOPY") != NULL;
        if (prof) {
            /* the clock's step, so the numbers can be judged */
            double a = now_us(), b;
            while ((b = now_us()) == a)
                ;
            fprintf(stderr, "s3d prof: gettimeofday steps %.0f us\n", b - a);
        }
        if (s3d_open() < 0)
            return -1;
    }
    if (w > screen_w)
        w = screen_w;
    if (h > fb_rows - BACK_ROW)
        h = fb_rows - BACK_ROW;
    width = w;
    height = h;

    /* bitmap: the off-screen rows at the screen's stride, so the blitter
       can copy the image straight into the window */
    blk->offset[0] = sc; blk->offset[1] = a_prefs;
    blk->offset[2] = board + FB_PHYS + (unsigned long) BACK_ROW * stride_px * 2;
    blk->format[0] = PIXFMT_RGB16;
    blk->x[0] = 0; blk->y[0] = 0; blk->x[1] = stride_px; blk->y[1] = h;
    op(OP_SETBITMAP);
    /* the Z buffer must be as wide as the bitmap's rows: CLEARZBUFFER
       clears bitmap-large x high entries, whatever was allocated */
    blk->offset[0] = sc; blk->x[0] = stride_px; blk->y[0] = h;
    if (!op(OP_ALLOCZBUFFER) && !check) {
        fprintf(stderr, "TinyGL: soft3D has no memory for a %dx%d Z buffer, drawing on the 68k\n", w, h);
        s3d_end();
        return -1;
    }
    blk->offset[0] = sc; blk->x[0] = 0; blk->x[1] = w - 1; blk->y[0] = 0; blk->y[1] = h - 1;
    op(OP_SETCLIPPING);

    nbatch = 0;
    {
        struct dstate d;
        d.usetex = 0; d.st = 0; d.blend = BLENDREPLACE;
        d.zmode = ZMODE(1, W3D_Z_LESS); d.texenv = W3D_MODULATE;
        set_state(&d);
    }
    s3d_active = 1;
    return 0;
}

/* n 32-byte vertices to the board, two move16 lines each (both ends are
   16-byte aligned); the 060 bursts a line where single longs go one by one */
static void to_board(struct point3D *dst, const struct point3D *src, int n)
{
    register const struct point3D *s __asm__("a0") = src;
    register struct point3D *d __asm__("a1") = dst;

    if (((unsigned long) src | (unsigned long) dst) & 15) {
        memcpy(dst, src, n * sizeof *src);      /* not line-aligned after all */
        return;
    }
    while (n-- > 0)
        __asm__ volatile (".word 0xf620,0x9000\n\t"   /* move16 (a0)+,(a1)+ */
                          ".word 0xf620,0x9000"         /* move16 (a0)+,(a1)+ */
                          : "+a" (s), "+a" (d) : : "memory");
}

static void flush_batch(void)
{
    if (nbatch == 0)
        return;
    if (prof) {
        double t0 = now_us();
        to_board(bbatch, batch, nbatch);
        pt_copy += now_us() - t0;
    } else if (!nocopy)
        to_board(bbatch, batch, nbatch);
    blk->offset[0] = sc; blk->offset[1] = a_batch;
    blk->format[0] = nbatch; blk->format[1] = W3D_PRIMITIVE_TRIANGLES;
    op(OP_DRAWPRIMITIVE);
    nbatch = 0;
}

static unsigned char to8(float f)
{
    return f <= 0.0f ? 0 : f >= 1.0f ? 255 : (unsigned char) (f * 255.0f);
}

static void put(GLContext *c, GLVertex *v, GLVertex *colour)
{
    struct point3D *p = &batch[nbatch++];
    float winv = 1.0f / v->pc.W;

    p->x = v->pc.X * winv * c->viewport.scale.X + c->viewport.trans.X;
    p->y = v->pc.Y * winv * c->viewport.scale.Y + c->viewport.trans.Y;
    p->z = 0.5f + 0.5f * v->pc.Z * winv;
    p->w = winv;
    p->u = v->tex_coord.X;
    p->v = v->tex_coord.Y;
    /* the firmware byte-reverses the colour long (COPYP_SWAP) and then
       reads it as R G B A: so A B G R here */
    p->RGBA[0] = to8(colour->color.v[3]);
    p->RGBA[1] = to8(colour->color.v[2]);
    p->RGBA[2] = to8(colour->color.v[1]);
    p->RGBA[3] = to8(colour->color.v[0]);
}

/* the ARM rasterizer trusts what it gets: keep the triangle only if every
   value is finite and on the bitmap (TinyGL has clipped it, so only
   rounding is let through, and pulled back on), and it has an area */
static int finite32(float f)            /* -ffast-math: isfinite is folded away */
{
    union { float f; unsigned long l; } u;
    u.f = f;
    return (u.l & 0x7f800000UL) != 0x7f800000UL;
}

static int sane(struct point3D *p)
{
    float a;
    int i, k;

    n_tri++;
    for (i = 0; i < 3; i++) {
        float f[4];
        f[0] = p[i].x; f[1] = p[i].y; f[2] = p[i].z; f[3] = p[i].w;
        for (k = 0; k < 4; k++) {
            if (!finite32(f[k]))
                goto bad;
            if (check) {
                if (f[k] < lo[k] || n_tri == 1) lo[k] = f[k];
                if (f[k] > hi[k] || n_tri == 1) hi[k] = f[k];
            }
        }
        if (!finite32(p[i].u) || !finite32(p[i].v))
            goto bad;
        if (p[i].x < -1.0f || p[i].x > width + 1.0f || p[i].y < -1.0f || p[i].y > height + 1.0f ||
            p[i].z < -0.01f || p[i].z > 1.01f || p[i].w <= 0.0f)
            goto bad;
        if (p[i].x < 0.0f) p[i].x = 0.0f;
        if (p[i].x > width - 1) p[i].x = width - 1;
        if (p[i].y < 0.0f) p[i].y = 0.0f;
        if (p[i].y > height - 1) p[i].y = height - 1;
        if (p[i].z < 0.0f) p[i].z = 0.0f;
        if (p[i].z > 1.0f) p[i].z = 1.0f;
    }
    a = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (a < 0.01f && a > -0.01f) {
        n_degen++;
        return 0;
    }
    return 1;
bad:
    n_bad++;
    return 0;
}

void s3d_triangle(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2)
{
    int smooth = c->current_shade_model == GL_SMOOTH;
    int usetex = c->texture_2d_enabled && c->current_texture != NULL;
    unsigned long st = 0;
    struct dstate d;

    if (usetex) {
        st = texture(&c->current_texture->images[0]);
        if (st == 0)
            usetex = 0;                             /* not uploadable: untextured */
    }
    want_state(c, usetex, st, !c->depth_test_set || c->depth_test, &d);
    if (!same_state(&d, &cur)) {
        flush_batch();
        set_state(&d);
    }
    if (nbatch + 3 > MAXPOINTS)
        flush_batch();
    /* flat: the last vertex's colour on all three, as TinyGL fills flat */
    put(c, p0, smooth ? p0 : p2);
    put(c, p1, smooth ? p1 : p2);
    put(c, p2, p2);
    if (!sane(&batch[nbatch - 3]))
        nbatch -= 3;
    else if (cur.blend != BLENDREPLACE)
        blend_order();
}

/* points and lines: soft3D is set up for triangles only, so a point is a
   one-pixel square and a line a one-pixel-wide quad, two triangles each,
   untextured, in the vertices' colours and depth */
static void emit(float x, float y, float z, float w, GLVertex *colour)
{
    struct point3D *p = &batch[nbatch++];

    p->x = x; p->y = y; p->z = z; p->w = w;
    p->u = 0.0f; p->v = 0.0f;
    p->RGBA[0] = to8(colour->color.v[3]);
    p->RGBA[1] = to8(colour->color.v[2]);
    p->RGBA[2] = to8(colour->color.v[1]);
    p->RGBA[3] = to8(colour->color.v[0]);
}

static void s3d_quad(GLContext *c, float ax, float ay, float az, float aw, GLVertex *ca,
                 float bx, float by, float bz, float bw, GLVertex *cb, float nx, float ny)
{
    struct dstate d;

    want_state(c, 0, 0, !c->depth_test_set || c->depth_test, &d);
    if (!same_state(&d, &cur)) {
        flush_batch();
        set_state(&d);
    }
    if (nbatch + 6 > MAXPOINTS)
        flush_batch();
    emit(ax - nx, ay - ny, az, aw, ca);
    emit(bx - nx, by - ny, bz, bw, cb);
    emit(bx + nx, by + ny, bz, bw, cb);
    if (!sane(&batch[nbatch - 3]))
        nbatch -= 3;
    else if (cur.blend != BLENDREPLACE)
        blend_order();
    emit(ax - nx, ay - ny, az, aw, ca);
    emit(bx + nx, by + ny, bz, bw, cb);
    emit(ax + nx, ay + ny, az, aw, ca);
    if (!sane(&batch[nbatch - 3]))
        nbatch -= 3;
    else if (cur.blend != BLENDREPLACE)
        blend_order();
}

static void screen(GLContext *c, GLVertex *v, float *x, float *y, float *z, float *w)
{
    float winv = 1.0f / v->pc.W;

    *x = v->pc.X * winv * c->viewport.scale.X + c->viewport.trans.X;
    *y = v->pc.Y * winv * c->viewport.scale.Y + c->viewport.trans.Y;
    *z = 0.5f + 0.5f * v->pc.Z * winv;
    *w = winv;
}

/* a point: one triangle whose legs are 2 pixels, so it covers the whole
   pixel square (half the work of the quad a line gets) */
void s3d_point(GLContext *c, GLVertex *p)
{
    float x, y, z, w;
    struct dstate d;

    screen(c, p, &x, &y, &z, &w);
    want_state(c, 0, 0, !c->depth_test_set || c->depth_test, &d);
    if (!same_state(&d, &cur)) {
        flush_batch();
        set_state(&d);
    }
    if (nbatch + 3 > MAXPOINTS)
        flush_batch();
    emit(x - 0.5f, y - 0.5f, z, w, p);
    emit(x + 1.5f, y - 0.5f, z, w, p);
    emit(x - 0.5f, y + 1.5f, z, w, p);
    if (!sane(&batch[nbatch - 3]))
        nbatch -= 3;
    else if (cur.blend != BLENDREPLACE)
        blend_order();
}

/* for programs: is soft3D drawing (not TINYGL_SOFT3D=0, and it started)? */
int glAmixSoft3D(void)
{
    return s3d_active;
}

void s3d_line(GLContext *c, GLVertex *p1, GLVertex *p2)
{
    float x1, y1, z1, w1, x2, y2, z2, w2, dx, dy, len;

    screen(c, p1, &x1, &y1, &z1, &w1);
    screen(c, p2, &x2, &y2, &z2, &w2);
    dx = x2 - x1;
    dy = y2 - y1;
    len = dx * dx + dy * dy;
    if (len < 0.25f) {
        s3d_point(c, p2);
        return;
    }
    len = 0.5f / (float) sqrt(len);
    s3d_quad(c, x1, y1, z1, w1, p1, x2, y2, z2, w2, p2, -dy * len, dx * len);
}

void s3d_clear(int color, int depth, float r, float g, float b)
{
    flush_batch();
    if (color && !noclear) {
        unsigned long rgb = ((unsigned long) to8(r) >> 3 << 11) | ((unsigned long) to8(g) >> 2 << 5) |
                            (to8(b) >> 3);
        blitx(0, 0, 0, 0, BACK_ROW, width, height, rgb);
    }
    if (depth) {
        float one = 1.0f;
        blk->offset[0] = sc;
        memcpy((void *) &blk->format[0], &one, 4);
        op(OP_CLEARZBUFFER);
    }
}

void s3d_swap(Display *dpy, Drawable win)
{
    int x, y, sx = 0, sy = BACK_ROW, w = width, h = height;
    Window child;

    flush_batch();
    if (check && ++n_frames % 50 == 0)
        fprintf(stderr, "s3d check: %lu tris, %lu bad, %lu degenerate; x %g..%g y %g..%g z %g..%g w %g..%g\n",
                n_tri, n_bad, n_degen, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], lo[3], hi[3]);
    blk->offset[0] = sc;
    op(OP_FLUSH);
    nov = 0;
    blk->offset[0] = sc;
    op(OP_DOUPDATE);

    if (noswap)
        return;
    if (xmajor > 0) {
        double t0 = prof ? now_us() : 0;
        s3d_xswap(dpy, xmajor, win, 0, BACK_ROW, w, h);
        if (prof) {
            double t1 = now_us();
            pt_swap += t1 - t0;
            if (pt_last.tv_sec)
                pt_frame += t1 - (pt_last.tv_sec * 1e6 + pt_last.tv_usec);
            gettimeofday(&pt_last, 0);
            if (++pn_frames == 100)
                prof_report();
        }
        return;
    }
    /* TINYGL_SOFT3D_DIRECT: straight onto the screen, over whatever is there */
    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &x, &y, &child);
    if (x < 0) { sx -= x; w += x; x = 0; }
    if (y < 0) { sy -= y; h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w > 0 && h > 0)
        blitx(1, sx, sy, x, y, w, h, 0);
}
