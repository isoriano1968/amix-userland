/*
** ZZ9000draw.c - ZZ9000 drawing primitives for xamix-rtg.
**
** Direct 16-bit pixel writes to the mmap'd framebuffer at
** pRTG->fbBase (pixel 0,0), pitch = ZZ9000_PITCH bytes per scanline.
**
** All span coordinates are absolute screen coordinates (miTranslate=TRUE).
** Rectangle coordinates from PolyFillRect are drawable-relative and
** are adjusted by pDrawable->x / pDrawable->y before use.
** Clipping uses rtgGCClip(pGC) which ValidateGC keeps up to date.
**
** Functions that operate without a GC (PaintWindow, CopyWindow) clip
** against the region passed by the caller.
*/

#include <string.h>
#include <errno.h>
#include "X.h"
#include "Xmd.h"
#include "scrnintstr.h"
#include "regionstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "servermd.h"
#include "mi.h"
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

/* Uncomment to dump the first 40 ZZ9000FillSpans() calls (y/x/width of
** each clipped span) to the server log - diagnostic for text/glyph
** rendering corruption.  Text and the software cursor both reach the
** framebuffer through FillSpans (via miPushPixels); general fills/rects
** go through ZZ9000SolidRect instead, so this stays fairly quiet outside
** of text and cursor drawing. Remove once done. */
/* #define ZZ9000_DEBUG_FILLSPANS */

/* ---------------------------------------------------------------------- */
/* Private helpers                                                         */

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define ZZ9000_STRIDE(pRTG)  ((pRTG)->pitch / 2)  /* pixels per scanline */

/* ---------------------------------------------------------------------- */
/* Run helpers (P-5 fix 2): 32-bit stores where both ends allow it.         */

/*
** A 32-bit store moves two pixels in one bus cycle where a 16-bit store moves
** one (Antti measured 0.41 against 0.79 us/pixel on the VA2000 over Zorro III).
** The high word is the left pixel (big-endian).  Only aligned longs are used:
** a misaligned 32-bit access to the card's space is split by the CPU in ways
** this driver does not want to depend on, so a copy whose two ends differ in
** alignment keeps the 16-bit loop.
*/
#define RUN_ODD(p)  (((unsigned long)(p)) & 2)

/*
** Below this many pixels the alignment work costs more than the longs save:
** dots, 1x1 rectangles and short line spans (x11perf -dot/-rect1/-line10
** lost 10-17 % to the 32-bit path on the rig), so short runs keep the plain
** 16-bit loop.
*/
#define RUN_SHORT   4

/* Fill w pixels with one colour. */
static void
fillRun(dst, pixel, w)
unsigned short *dst;
unsigned short  pixel;
int             w;
{
    unsigned long  two;
    unsigned long *l;
    int            n;

    if (w < RUN_SHORT) {
        while (w-- > 0)
            *dst++ = pixel;
        return;
    }
    if (RUN_ODD(dst)) {
        *dst++ = pixel;
        w--;
    }
    two = ((unsigned long)pixel << 16) | pixel;
    l = (unsigned long *)dst;
    for (n = w >> 1; n > 0; n--)
        *l++ = two;
    if (w & 1)
        *(unsigned short *)l = pixel;
}

/* Copy w pixels, left to right. */
static void
copyRun(dst, src, w)
unsigned short *dst;
unsigned short *src;
int             w;
{
    unsigned long *ld, *ls;
    int            n;

    if (w < RUN_SHORT || RUN_ODD(dst) != RUN_ODD(src)) {
        while (w-- > 0)
            *dst++ = *src++;
        return;
    }
    if (RUN_ODD(dst)) {
        *dst++ = *src++;
        w--;
    }
    ld = (unsigned long *)dst;
    ls = (unsigned long *)src;
    for (n = w >> 1; n > 0; n--)
        *ld++ = *ls++;
    if (w & 1)
        *(unsigned short *)ld = *(unsigned short *)ls;
}

/* Copy w pixels, right to left (an overlapping move to the right). */
static void
copyRunBack(dst, src, w)
unsigned short *dst;
unsigned short *src;
int             w;
{
    unsigned long *ld, *ls;
    int            n;

    if (w <= 0)
        return;
    dst += w;                       /* one past the last pixel */
    src += w;
    if (w < RUN_SHORT || RUN_ODD(dst) != RUN_ODD(src)) {
        while (w-- > 0)
            *--dst = *--src;
        return;
    }
    if (RUN_ODD(dst)) {
        *--dst = *--src;
        w--;
    }
    ld = (unsigned long *)dst;
    ls = (unsigned long *)src;
    for (n = w >> 1; n > 0; n--)
        *--ld = *--ls;
    if (w & 1)
        *((unsigned short *)ld - 1) = *((unsigned short *)ls - 1);
}

/* ---------------------------------------------------------------------- */
/* CPU rectangle helpers                                                   */

static void
cpuFill(pRTG, x1, y1, x2, y2, color)
rtgScreenPtr   pRTG;
unsigned short x1, y1, x2, y2, color;
{
    unsigned short *base;
    unsigned short *p;
    int stride;
    int x, y;

    base = pRTG->fbBase;
    stride = ZZ9000_selected_mode->w;

    for (y = y1; y <= y2; ++y) {
        p = base + ((unsigned long)y * stride) + x1;
        fillRun(p, color, x2 - x1 + 1);
    }
}
/*
** Pixel base and stride for a drawable (window -> VRAM; pixmap -> host mem).
*/
static unsigned short *
drawBase(pDraw, pRTG)
DrawablePtr  pDraw;
rtgScreenPtr pRTG;
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pRTG->fbBase;
    return (unsigned short *) ((PixmapPtr)pDraw)->devPrivate.ptr;
}

static int
drawStride(pDraw)
DrawablePtr pDraw;
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return ZZ9000_STRIDE(GetRTGScreen(pDraw->pScreen));
    return (int)((PixmapPtr)pDraw)->devKind / 2;
}

/*
** cpuCopy - copy a screen rectangle in mapped framebuffer memory, rows and
** columns ordered so that an overlapping move reads every source pixel
** before it is overwritten.
*/
static void
cpuCopy(pRTG, srcx, srcy, w, h, dstx, dsty)
rtgScreenPtr    pRTG;
int             srcx, srcy, w, h, dstx, dsty;
{
    unsigned short *fbBase = pRTG->fbBase;
    int             stride = ZZ9000_STRIDE(pRTG);
    unsigned short *src;
    unsigned short *dst;
    int row, col;
    int row_start, row_end, row_step;
    int col_start, col_end, col_step;

    if (w <= 0 || h <= 0)
        return;

    if (dsty > srcy) {
        row_start = h - 1; row_end = -1; row_step = -1;
    } else {
        row_start = 0; row_end = h; row_step = 1;
    }

    if (dsty == srcy && dstx > srcx) {
        col_start = w - 1; col_end = -1; col_step = -1;
    } else {
        col_start = 0; col_end = w; col_step = 1;
    }

    for (row = row_start; row != row_end; row += row_step) {
        src = fbBase + ((unsigned long)(srcy + row) * stride) + srcx;
        dst = fbBase + ((unsigned long)(dsty + row) * stride) + dstx;
        if (col_step < 0)
            copyRunBack(dst, src, w);
        else
            copyRun(dst, src, w);
    }
}

/* ---------------------------------------------------------------------- */
/* The card's blitter (P-5 fix 3)                                          */

/*
** -blitmin N: window fills (SolidRect GXcopy, PaintWindow) and window-to-
** window copies (CopyArea GXcopy, CopyWindow) of N pixels or more go to the
** card's own blitter through the driver's ZZ9KIOC_BLITS; anything smaller,
** any other raster op and every pixmap stay on the CPU.  -blitmin 0 never
** blits.  The default, 1024, is MG10's: it keeps the 100x100 gains (rect100
** 585 -> 8140/s, scroll100 311 -> 2260) that 16384 gives up.
**
** -blitmax N: every record covers at most N pixels (whole rows; a downward
** copy's bands run bottom-up).  The CPU sits in the stalled register write
** for the whole of one card operation and takes no interrupt: MG10 measured
** ~20 ms for a full-screen op and lost 42-44 % of serial input at 9600 and
** 38400.  The driver runs a batch record by record at base priority, so
** interrupts are taken between bands.  The default, 8192, is ~0.16 ms at
** the measured ~50,000 px/ms -- under one character time at 38400 (0.26 ms;
** Paula has a one-byte receive buffer).  0 does not split.
**
** Records are queued and sent in batches.  The queue keeps drawing order:
** it is flushed before any CPU drawing in these paths and before each entry
** point returns, so no other code ever finds it non-empty.  If the driver
** refuses (no blitter on this board or in this mode: ENXIO; a kernel
** without the ioctl: EINVAL), blitting stays off for the life of the server
** and the refused records are drawn by the CPU.  The driver refuses a batch
** before running any of it, and records made from clip boxes always lie on
** the screen, so nothing is drawn twice.
*/
int zz9000BlitMin = 1024;
int zz9000BlitMax = 8192;

#define BLITQ   64

static struct zz9k_blit blitQ[BLITQ];
static int              blitN;
static rtgScreenPtr     blitRTG;
static int              blitSaid;       /* the server log has the first batch */

/*
** The common case -- no -blitmin, or a small area -- must cost no more than
** before the blitter existed (MG10: -rect1 lost 20 % to three extra calls
** per 1x1 fill), so the tests are macros and a flush of an empty queue is
** one compare.
*/
#define BLIT_WANT(w, h) (zz9000BlitMin > 0 && (long)(w) * (h) >= zz9000BlitMin)
#define BLIT_FLUSH()    do { if (blitN) blitFlush(); } while (0)

static void
blitFlush()
{
    struct zz9k_blits bs;
    struct zz9k_blit *b;
    int               i, n;

    if (blitN == 0)
        return;
    bs.recs = blitQ;
    bs.n    = blitN;
    if (ioctl(GetZZ9000Screen(blitRTG)->fd, ZZ9KIOC_BLITS, &bs) >= 0)
    {
        if (!blitSaid)
            ErrorF("zz9000: card blitter in use from %d pixels\n", zz9000BlitMin);
        blitSaid = 1;
        blitN = 0;
        return;
    }
    ErrorF("zz9000: the driver refused the card blitter (errno %d); -blitmin %d off\n",
           errno, zz9000BlitMin);
    zz9000BlitMin = 0;
    n = blitN;
    blitN = 0;
    for (i = 0, b = blitQ; i < n; i++, b++)
    {
        if (b->op == ZZ9K_BLIT_FILL)
            cpuFill(blitRTG, b->x, b->y, b->x + b->width - 1,
                    b->y + b->height - 1, (unsigned short) b->color);
        else
            cpuCopy(blitRTG, b->src_x, b->src_y, b->width, b->height,
                    b->x, b->y);
    }
}

/* Append one record (already sized) to the queue; FALSE = refused. */
static int
blitRecord(pRTG, op, x, y, w, h, sx, sy, color)
rtgScreenPtr   pRTG;
int            op, x, y, w, h, sx, sy;
unsigned short color;
{
    struct zz9k_blit *b;

    if (blitN == BLITQ)
        blitFlush();
    if (zz9000BlitMin <= 0)         /* that flush was refused */
        return FALSE;
    b = &blitQ[blitN++];
    b->op     = op;
    b->x      = x;
    b->y      = y;
    b->width  = w;
    b->height = h;
    b->src_x  = sx;
    b->src_y  = sy;
    b->pad    = 0;
    b->color  = color;
    blitRTG   = pRTG;
    return TRUE;
}

/*
** Queue one blitter operation when the area qualifies, in bands of at most
** -blitmax pixels; FALSE = draw it yourself.  A refusal comes from a flush
** (the first one of the server, which a full queue can trigger in the middle
** of a banded operation); that flush has already drawn every queued record,
** earlier bands included, on the CPU, so the rows not yet queued are drawn
** here on the CPU too -- redrawing the whole operation would move an
** overlapping copy twice.
*/
static int
blitQueue(pRTG, op, x, y, w, h, sx, sy, color)
rtgScreenPtr   pRTG;
int            op, x, y, w, h, sx, sy;
unsigned short color;
{
    int band, top, n;

    if (zz9000BlitMin <= 0 || (long) w * h < zz9000BlitMin)
        return FALSE;
    band = h;
    if (zz9000BlitMax > 0 && (long) w * h > zz9000BlitMax) {
        band = zz9000BlitMax / w;
        if (band < 1)
            band = 1;
    }
    if (op == ZZ9K_BLIT_COPY && y > sy) {       /* moving down: bottom band first */
        for (top = h; top > 0; top -= n) {
            n = top < band ? top : band;
            if (!blitRecord(pRTG, op, x, y + top - n, w, n, sx, sy + top - n, color)) {
                if (top == h)
                    return FALSE;
                cpuCopy(pRTG, sx, sy, w, top, x, y);   /* rows [0, top) */
                return TRUE;
            }
        }
        return TRUE;
    }
    for (top = 0; top < h; top += n) {
        n = h - top < band ? h - top : band;
        if (!blitRecord(pRTG, op, x, y + top, w, n, sx, sy + top, color)) {
            if (top == 0)
                return FALSE;
            if (op == ZZ9K_BLIT_FILL)                   /* rows [top, h) */
                cpuFill(pRTG, x, y + top, x + w - 1, y + h - 1, color);
            else
                cpuCopy(pRTG, sx, sy + top, w, h - top, x, y + top);
            return TRUE;
        }
    }
    return TRUE;
}

/* Fill the inclusive screen box (x1,y1)-(x2,y2): the blitter or the CPU. */
static void
blitFill(pRTG, x1, y1, x2, y2, color)
rtgScreenPtr   pRTG;
unsigned short x1, y1, x2, y2, color;
{
    if (BLIT_WANT(x2 - x1 + 1, y2 - y1 + 1) &&
        blitQueue(pRTG, ZZ9K_BLIT_FILL, x1, y1, x2 - x1 + 1, y2 - y1 + 1,
                  0, 0, color))
        return;
    BLIT_FLUSH();
    cpuFill(pRTG, x1, y1, x2, y2, color);
}

/* Copy a w x h screen box from (srcx,srcy) to (dstx,dsty): blitter or CPU. */
static void
copyVRAMBox(pRTG, srcx, srcy, w, h, dstx, dsty)
rtgScreenPtr pRTG;
int          srcx, srcy, w, h, dstx, dsty;
{
    if (w <= 0 || h <= 0)
        return;
    if (BLIT_WANT(w, h) &&
        blitQueue(pRTG, ZZ9K_BLIT_COPY, dstx, dsty, w, h, srcx, srcy, 0))
        return;
    BLIT_FLUSH();
    cpuCopy(pRTG, srcx, srcy, w, h, dstx, dsty);
}

/* ---------------------------------------------------------------------- */
/* ZZ9000GetImage - tracing wrapper for miGetImage                        */

extern void miGetImage();
extern void mfbGetSpans();

void
ZZ9000GetImage(pDraw, sx, sy, w, h, format, planeMask, pdstLine)
DrawablePtr    pDraw;
int            sx, sy, w, h;
unsigned int   format;
unsigned long  planeMask;
pointer        pdstLine;
{
    miGetImage(pDraw, sx, sy, w, h, format, planeMask, pdstLine);
}

/* ---------------------------------------------------------------------- */
/* ZZ9000FillSpans                                                         */

/*
** ZZ9000FillSpans - solid colour span fill.
**
** Called by DIX for FillSolid fill style.  Spans in ppts are in absolute
** screen coordinates (DIX translates when miTranslate=TRUE).
** miClipSpans may split one input span across several clip boxes, so its
** output must use separately allocated arrays sized for the widest band.
*/
void
ZZ9000FillSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
int         nspans;
DDXPointPtr ppts;
int        *pwidths;
int         fSorted;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short  pixel  = (unsigned short) pGC->fgPixel;
    int             alu    = pGC->alu;
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    RegionPtr       clip   = rtgGCClip(pGC);
    DDXPointPtr     clippedPts;
    int            *clippedWidths;
    int             maxSpans;
    unsigned short *p;
    int             i, w;

    if (nspans <= 0)
        return;

    maxSpans = nspans * miFindMaxBand(clip);
    if (maxSpans <= 0)
        return;

    clippedPts = (DDXPointPtr)
        ALLOCATE_LOCAL(maxSpans * sizeof(DDXPointRec));
    clippedWidths = (int *) ALLOCATE_LOCAL(maxSpans * sizeof(int));
    if (!clippedPts || !clippedWidths)
    {
        if (clippedPts)
            DEALLOCATE_LOCAL(clippedPts);
        if (clippedWidths)
            DEALLOCATE_LOCAL(clippedWidths);
        return;
    }

    nspans = miClipSpans(clip,
                         ppts, pwidths, nspans,
                         clippedPts, clippedWidths, fSorted);

#ifdef ZZ9000_DEBUG_FILLSPANS
    {
        static int dbgCalls = 0;
        if (dbgCalls < 40)
        {
            dbgCalls++;
            ErrorF("ZZ9000FillSpans[%d]: nspans=%d pixel=0x%04x alu=%d stride=%d\n",
                   dbgCalls, nspans, pixel, alu, stride);
            for (i = 0; i < nspans && i < 20; i++)
                ErrorF("  span[%d]: y=%d x=%d w=%d\n",
                       i, clippedPts[i].y, clippedPts[i].x, clippedWidths[i]);
        }
    }
#endif

    for (i = 0; i < nspans; i++)
    {
        w = clippedWidths[i];
        if (w <= 0)
            continue;
        p = base + clippedPts[i].y * stride + clippedPts[i].x;

        switch (alu)
        {
        case GXcopy:         fillRun(p, pixel, w);                    break;
        case GXxor:          while (w-->0) { *p ^= pixel;    p++; }    break;
        case GXor:           while (w-->0) { *p |= pixel;    p++; }    break;
        case GXand:          while (w-->0) { *p &= pixel;    p++; }    break;
        case GXinvert:       while (w-->0) { *p ^= 0xFFFF;  p++; }    break;
        case GXclear:        while (w-->0)  *p++ = 0;                  break;
        case GXset:          while (w-->0)  *p++ = 0xFFFF;             break;
        case GXnoop:                                                    break;
        case GXcopyInverted: while (w-->0)  *p++ = ~pixel;             break;
        case GXandReverse:   while (w-->0) { *p = pixel & ~*p;  p++; } break;
        case GXandInverted:  while (w-->0) { *p &= ~pixel;      p++; } break;
        case GXorReverse:    while (w-->0) { *p = pixel | ~*p;  p++; } break;
        case GXnor:          while (w-->0) { *p = ~(pixel | *p); p++; } break;
        case GXequiv:        while (w-->0) { *p = ~(pixel ^ *p); p++; } break;
        case GXorInverted:   while (w-->0) { *p |= ~pixel;      p++; } break;
        case GXnand:         while (w-->0) { *p = ~(pixel & *p); p++; } break;
        default:             fillRun(p, pixel, w);                    break;
        }
    }

    DEALLOCATE_LOCAL(clippedWidths);
    DEALLOCATE_LOCAL(clippedPts);
}

/* ---------------------------------------------------------------------- */
/* ZZ9000SetSpans                                                          */

/*
** ropRun - combine w source pixels into dst under a raster op (not GXcopy).
*/
static void
ropRun(dst, src, w, alu)
unsigned short *dst;
unsigned short *src;
int             w;
int             alu;
{
    unsigned short s, d;

    while (w-- > 0)
    {
        s = *src++;
        d = *dst;
        switch (alu)
        {
        case GXclear:        d = 0;           break;
        case GXand:          d = s & d;       break;
        case GXandReverse:   d = s & ~d;      break;
        case GXcopy:         d = s;           break;
        case GXandInverted:  d = ~s & d;      break;
        case GXnoop:                          break;
        case GXxor:          d = s ^ d;       break;
        case GXor:           d = s | d;       break;
        case GXnor:          d = ~(s | d);    break;
        case GXequiv:        d = ~s ^ d;      break;
        case GXinvert:       d = ~d;          break;
        case GXorReverse:    d = s | ~d;      break;
        case GXcopyInverted: d = ~s;          break;
        case GXorInverted:   d = ~s | d;      break;
        case GXnand:         d = ~(s & d);    break;
        case GXset:          d = 0xFFFF;      break;
        }
        *dst++ = d;
    }
}

/*
** ZZ9000PatternSpans - FillSpans for FillTiled, FillStippled and
** FillOpaqueStippled, on windows (VRAM) and pixmaps (host memory) alike.
**
** The pattern origin is the drawable's origin plus the GC's patOrg.  A tile
** is a 16-bit pixmap; a stipple is a depth-1 pixmap, MSBFirst bit order
** (servermd.h for AMIX), so bit x of a row is row[x>>3] & (0x80 >> (x&7)).
** FillStippled leaves clear bits alone; FillOpaqueStippled paints them with
** the background.  These used to fall back to a solid foreground fill, which
** made every tiled rectangle (dtstyle's backdrop preview) and every stippled
** one (Motif's insensitive text and shading) a plain block.
*/
void
ZZ9000PatternSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
int         nspans;
DDXPointPtr ppts;
int        *pwidths;
int         fSorted;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    RegionPtr       clip   = rtgGCClip(pGC);
    int             alu    = pGC->alu;
    int             tiled  = (pGC->fillStyle == FillTiled);
    int             opaque = (pGC->fillStyle == FillOpaqueStippled);
    PixmapPtr       pPat   = tiled ? pGC->tile.pixmap : pGC->stipple;
    unsigned short  fg     = (unsigned short) pGC->fgPixel;
    unsigned short  bg     = (unsigned short) pGC->bgPixel;
    int             xorg, yorg, pw, ph, pstride;
    DDXPointPtr     clippedPts;
    int            *clippedWidths;
    int             maxSpans, i, w, n, x, px, py;
    unsigned short *p;
    unsigned char  *srow;
    unsigned short *trow;

    if (nspans <= 0 || !pPat || !pPat->devPrivate.ptr ||
        pPat->drawable.width <= 0 || pPat->drawable.height <= 0 ||
        (tiled && pPat->drawable.depth != ZZ9000_DEPTH) ||
        (!tiled && pPat->drawable.depth != 1))
    {
        ZZ9000FillSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted);
        return;
    }
    pw = pPat->drawable.width;
    ph = pPat->drawable.height;
    pstride = pPat->devKind;            /* bytes */
    xorg = pDraw->x + pGC->patOrg.x;
    yorg = pDraw->y + pGC->patOrg.y;

    maxSpans = nspans * miFindMaxBand(clip);
    if (maxSpans <= 0)
        return;
    clippedPts = (DDXPointPtr) ALLOCATE_LOCAL(maxSpans * sizeof(DDXPointRec));
    clippedWidths = (int *) ALLOCATE_LOCAL(maxSpans * sizeof(int));
    if (!clippedPts || !clippedWidths)
    {
        if (clippedPts)
            DEALLOCATE_LOCAL(clippedPts);
        if (clippedWidths)
            DEALLOCATE_LOCAL(clippedWidths);
        return;
    }
    nspans = miClipSpans(clip, ppts, pwidths, nspans,
                         clippedPts, clippedWidths, fSorted);
    if (pDraw->type == DRAWABLE_WINDOW)
        BLIT_FLUSH();                   /* no queued blit may land on top */

    for (i = 0; i < nspans; i++)
    {
        w = clippedWidths[i];
        if (w <= 0)
            continue;
        x = clippedPts[i].x;
        p = base + (long) clippedPts[i].y * stride + x;
        py = (clippedPts[i].y - yorg) % ph;
        if (py < 0)
            py += ph;
        px = (x - xorg) % pw;
        if (px < 0)
            px += pw;

        if (tiled)
        {
            trow = (unsigned short *)((char *) pPat->devPrivate.ptr + (long) py * pstride);
            for (; w > 0; w -= n, p += n, px = 0)
            {
                n = pw - px;
                if (n > w)
                    n = w;
                if (alu == GXcopy)
                    copyRun(p, trow + px, n);
                else
                    ropRun(p, trow + px, n, alu);
            }
        }
        else
        {
            srow = (unsigned char *) pPat->devPrivate.ptr + (long) py * pstride;
            for (; w > 0; w--, p++)
            {
                if (srow[px >> 3] & (0x80 >> (px & 7)))
                {
                    if (alu == GXcopy)
                        *p = fg;
                    else
                        ropRun(p, &fg, 1, alu);
                }
                else if (opaque)
                {
                    if (alu == GXcopy)
                        *p = bg;
                    else
                        ropRun(p, &bg, 1, alu);
                }
                if (++px == pw)
                    px = 0;
            }
        }
    }

    DEALLOCATE_LOCAL(clippedWidths);
    DEALLOCATE_LOCAL(clippedPts);
}

/*
** ZZ9000SetSpans - write packed 16-bit pixel data to the framebuffer.
**
** Each source scanline is padded according to PixmapBytePad(), as required
** by the DDX SetSpans contract.  For each input span we clip each clip-box
** intersection and copy the appropriate slice of the source.  GXcopy is a
** plain copy; every other raster op goes through ropRun (mi's PutImage and
** CopyArea paths land here with the GC's alu).
*/
void
ZZ9000SetSpans(pDraw, pGC, pcharsrc, ppt, pwidths, nspans, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
char       *pcharsrc;
DDXPointPtr ppt;
int        *pwidths;
int         nspans;
int         fSorted;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    RegionPtr       clip   = rtgGCClip(pGC);
    BoxPtr          pbox;
    int             nbox;
    char           *psrcLine;
    unsigned short *psrc;
    int             srcBytes;
    int             i, cx1, cx2, sw;

    psrcLine = pcharsrc;

    for (i = 0; i < nspans; i++, ppt++, pwidths++)
    {
        sw = *pwidths;
        srcBytes = sw > 0 ? PixmapBytePad(sw, pDraw->depth) : 0;
        if (sw <= 0)
        {
            continue;
        }

        psrc = (unsigned short *) psrcLine;

        nbox = REGION_NUM_RECTS(clip);
        pbox = REGION_RECTS(clip);

        while (nbox--)
        {
            if (pbox->y1 > ppt->y || pbox->y2 <= ppt->y)
            {
                pbox++;
                continue;
            }

            cx1 = max(ppt->x,      pbox->x1);
            cx2 = min(ppt->x + sw, pbox->x2);

            if (cx1 < cx2 && pGC->alu == GXcopy)
                copyRun(base + ppt->y * stride + cx1,
                        psrc + (cx1 - ppt->x), cx2 - cx1);
            else if (cx1 < cx2)
                ropRun(base + ppt->y * stride + cx1,
                       psrc + (cx1 - ppt->x), cx2 - cx1, pGC->alu);
            pbox++;
        }
        psrcLine += srcBytes;
    }
}

/* ---------------------------------------------------------------------- */
/* ZZ9000PutImage                                                          */

/*
** -move16 turns the move16 line burst on (68040/68060 only: a 68030 has no
** move16, so it is off by default).  Measured on the A3000 with
** the Z3660's 68060 (bench/vrambw.c, 2026-10-25): single long writes reach
** about 10.6 MB/s into VRAM and into RAM alike (the data cache writes
** through), move16 26 MB/s into VRAM.  move16 wants source and destination
** on 16-byte boundaries, so it only applies when both rows have the same
** phase; staging a misaligned row first costs as much as the plain copy.
*/
int zz9000Move16 = 0;	/* -move16: 68040/68060 only, a 68030 has no move16 */

/*
** burstRun - copyRun for a host-memory source: the 16-byte-aligned middle
** moved by move16 when src and dst share their phase in a line, misaligned
** long reads when they differ by two bytes.  move16 (a0)+,(a1)+ is spelled
** as words: the AMIX assembler knows no 68040 mnemonics.
*/
static void
burstRun(dst, src, w)
unsigned short *dst;
unsigned short *src;
int             w;
{
    register unsigned short *s asm("a0");
    register unsigned short *d asm("a1");
    int head, k;

    if (w >= RUN_SHORT && RUN_ODD(dst) != RUN_ODD(src))
    {
        /*
        ** Ends two bytes apart (an odd window x): copyRun would move one
        ** pixel per store, 59 ms for 512x320 where longs take 34.  The
        ** source of a PutImage is host memory, never the card, so its longs
        ** may be read misaligned (the 060 splits them, mostly from the
        ** cache); every store to the drawable stays an aligned long.
        */
        unsigned long *ld;
        int n;

        if (RUN_ODD(dst))
        {
            *dst++ = *src++;
            w--;
        }
        ld = (unsigned long *) dst;
        for (n = w >> 1; n > 0; n--, src += 2)
            *ld++ = *(unsigned long *) src;
        if (w & 1)
            *(unsigned short *) ld = *src;
        return;
    }
    if (!zz9000Move16 || w < 16 ||
        (((unsigned long) dst ^ (unsigned long) src) & 15) != 0)
    {
        copyRun(dst, src, w);
        return;
    }
    head = ((16 - ((unsigned long) dst & 15)) & 15) >> 1;
    if (head)
        copyRun(dst, src, head);
    s = src + head;
    d = dst + head;
    w -= head;
    for (k = w >> 3; k > 0; k--)
        __asm__ volatile (".word 0xf620,0x9000"     /* move16 (a0)+,(a1)+ */
                          : "=a" (s), "=a" (d) : "0" (s), "1" (d) : "memory");
    if (w & 7)
        copyRun(d, s, w & 7);
}

/*
** ZZ9000PutImage - a 16-bit ZPixmap under GXcopy (every XPutImage of a
** picture, and MIT-SHM's: Mesa's back buffer), straight from the request
** or the shared segment into the drawable, one clip-box intersection at a
** time.  miPutImage built a span list and went through SetSpans, which
** rescanned the clip list per row: about 12 ms of a 43 ms 512x320 swap.
** Everything else (bitmaps, XYPixmap, other raster ops, a plane mask)
** stays with miPutImage.
*/
void
ZZ9000PutImage(pDraw, pGC, depth, x, y, w, h, leftPad, format, pImage)
DrawablePtr pDraw;
GCPtr       pGC;
int         depth, x, y, w, h, leftPad, format;
char       *pImage;
{
    rtgScreenPtr    pRTG;
    unsigned short *base;
    int             stride, srcPix, nbox, x1, x2, y1, y2, row;
    RegionPtr       clip;
    BoxPtr          pbox;
    unsigned short *src;

    if (format != ZPixmap || depth != ZZ9000_DEPTH || pGC->alu != GXcopy ||
        (pGC->planemask & 0xffff) != 0xffff || w <= 0 || h <= 0)
    {
        miPutImage(pDraw, pGC, depth, x, y, w, h, leftPad, format, pImage);
        return;
    }
    pRTG   = GetRTGScreen(pDraw->pScreen);
    base   = drawBase(pDraw, pRTG);
    stride = drawStride(pDraw);
    clip   = rtgGCClip(pGC);
    srcPix = PixmapBytePad(w, depth) / 2;
    if (pGC->miTranslate)               /* as miPutImage: the clip's frame */
    {
        x += pDraw->x;
        y += pDraw->y;
    }
    if (pDraw->type == DRAWABLE_WINDOW)
        BLIT_FLUSH();                   /* no queued blit may land on top */

    nbox = REGION_NUM_RECTS(clip);
    pbox = REGION_RECTS(clip);
    for (; nbox--; pbox++)
    {
        x1 = max(x, pbox->x1);
        x2 = min(x + w, pbox->x2);
        y1 = max(y, pbox->y1);
        y2 = min(y + h, pbox->y2);
        if (x1 >= x2 || y1 >= y2)
            continue;
        src = (unsigned short *) pImage + (long)(y1 - y) * srcPix + (x1 - x);
        for (row = y1; row < y2; row++, src += srcPix)
            burstRun(base + (long) row * stride + x1, src, x2 - x1);
    }
}

/* ---------------------------------------------------------------------- */
/* ZZ9000GetSpans                                                          */

/*
** ZZ9000GetSpans - read pixel data from the drawable into pdstStart.
**
** No clipping: DIX clips before calling GetSpans.  Every returned scanline
** is padded independently according to PixmapBytePad().
*/
void
ZZ9000GetSpans(pDraw, wMax, ppt, pwidth, nspans, pdstStart)
DrawablePtr    pDraw;
int            wMax;
DDXPointPtr    ppt;
int           *pwidth;
int            nspans;
unsigned long *pdstStart;
{
    rtgScreenPtr    pRTG;
    unsigned short *base;
    int             stride;
    char           *dst;
    int             i, w, dstBytes;

    if (pDraw->depth == 1)
    {
        mfbGetSpans(pDraw, wMax, ppt, pwidth, nspans, pdstStart);
        return;
    }

    pRTG   = GetRTGScreen(pDraw->pScreen);
    base   = drawBase(pDraw, pRTG);
    stride = drawStride(pDraw);
    dst    = (char *) pdstStart;

    for (i = 0; i < nspans; i++, ppt++, pwidth++)
    {
        w = min(*pwidth, wMax);
        if (w <= 0)
            continue;
        dstBytes = PixmapBytePad(w, pDraw->depth);
        memset(dst, 0, (size_t)dstBytes);
        copyRun((unsigned short *)dst, base + ppt->y * stride + ppt->x, w);
        dst += dstBytes;
    }
}

/* ---------------------------------------------------------------------- */
/* ZZ9000SolidRect                                                         */

/*
** ZZ9000SolidRect - PolyFillRect with FillSolid.
**
** prects coordinates are drawable-relative; we add pDraw->x/y to get
** absolute screen coordinates before clipping against the GC clip.
**
** Window drawables: direct framebuffer fill.
** Pixmap drawables: CPU fill (blitter addresses VRAM, not host memory).
**
** Only GXcopy is filled here; the loops below write fgPixel and cannot
** apply a raster op.  Any other alu goes through miPolyFillRect, which
** calls this GC's FillSpans (ZZ9000FillSpans implements all sixteen), so
** a GXxor rubber band drawn twice erases itself instead of staying on
** screen.
*/
extern void miPolyFillRect();

void
ZZ9000SolidRect(pDraw, pGC, nrects, prects)
DrawablePtr pDraw;
GCPtr       pGC;
int         nrects;
xRectangle *prects;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short  pixel  = (unsigned short) pGC->fgPixel;
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    RegionPtr       clip   = rtgGCClip(pGC);
    int             useHW  = (pDraw->type == DRAWABLE_WINDOW &&
                              pGC->alu == GXcopy);
    BoxPtr          pbox;
    int             nbox;
    int             xorg   = pDraw->x;
    int             yorg   = pDraw->y;
    int             rx1, ry1, rx2, ry2;
    int             cx1, cy1, cx2, cy2;
    int             y, w, h;

    if (pGC->alu != GXcopy)
    {
        miPolyFillRect(pDraw, pGC, nrects, prects);
        return;
    }

    while (nrects--)
    {
        rx1 = xorg + (int) prects->x;
        ry1 = yorg + (int) prects->y;
        rx2 = rx1  + (int) prects->width;
        ry2 = ry1  + (int) prects->height;
        prects++;

        nbox = REGION_NUM_RECTS(clip);
        pbox = REGION_RECTS(clip);

        while (nbox--)
        {
            cx1 = max(rx1, pbox->x1);
            cy1 = max(ry1, pbox->y1);
            cx2 = min(rx2, pbox->x2);
            cy2 = min(ry2, pbox->y2);

            if (cx1 < cx2 && cy1 < cy2)
            {
                if (useHW && BLIT_WANT(cx2 - cx1, cy2 - cy1))
                {
                    blitFill(pRTG,
                             (unsigned short) cx1,
                             (unsigned short) cy1,
                             (unsigned short)(cx2 - 1),
                             (unsigned short)(cy2 - 1),
                             pixel);
                }
                else
                {
                    w = cx2 - cx1;
                    h = cy2 - cy1;
                    BLIT_FLUSH();
                    for (y = cy1; y < cy1 + h; y++)
                        fillRun(base + y * stride + cx1, pixel, w);
                }
            }
            pbox++;
        }
    }
    BLIT_FLUSH();
}

/* ---------------------------------------------------------------------- */
/* ZZ9000PaintWindow                                                       */

/*
** tileBoxes - fill the boxes from a 16-bit tile, the tile's origin at
** (xorg, yorg) as X requires (a window background's origin is the window's).
** Row by row through the CPU mapping: one copyRun per tile-wide stretch.
** Queued blits are flushed first so they cannot land on top of these writes.
*/
static void
tileBoxes(pRTG, pbox, nbox, pTile, xorg, yorg)
rtgScreenPtr pRTG;
BoxPtr       pbox;
int          nbox;
PixmapPtr    pTile;
int          xorg, yorg;
{
    unsigned short *fb = pRTG->fbBase;
    int             stride = ZZ9000_STRIDE(pRTG);
    unsigned short *tbase = (unsigned short *) pTile->devPrivate.ptr;
    int             tstride = (int) pTile->devKind / 2;
    int             tw = pTile->drawable.width, th = pTile->drawable.height;
    int             x, y, tx, ty, w, n;
    unsigned short *dst, *trow;

    BLIT_FLUSH();
    for (; nbox-- > 0; pbox++)
    {
        if (pbox->x1 >= pbox->x2 || pbox->y1 >= pbox->y2)
            continue;
        ty = (pbox->y1 - yorg) % th;
        if (ty < 0)
            ty += th;
        for (y = pbox->y1; y < pbox->y2; y++)
        {
            trow = tbase + (long) ty * tstride;
            dst = fb + (unsigned long) y * stride + pbox->x1;
            tx = (pbox->x1 - xorg) % tw;
            if (tx < 0)
                tx += tw;
            for (w = pbox->x2 - pbox->x1; w > 0; w -= n, dst += n, tx = 0)
            {
                n = tw - tx;
                if (n > w)
                    n = w;
                copyRun(dst, trow + tx, n);
            }
            if (++ty == th)
                ty = 0;
        }
    }
}

/*
** ZZ9000PaintWindow - paint window background (PW_BACKGROUND) or border
** (PW_BORDER).
**
** A solid pixel is a VRAM fill; a pixmap background or border is tiled from
** the window's origin (tileBoxes).  ParentRelative takes the nearest
** ancestor's background, with that ancestor's origin.  This used to fill
** pixmap backgrounds with the tile's first pixel, which made every CDE
** backdrop (a full-screen window with a background tile) a plain colour.
*/
void
ZZ9000PaintWindow(pWin, prgn, what)
WindowPtr pWin;
RegionPtr prgn;
int       what;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pWin->drawable.pScreen);
    unsigned short  pixel;
    PixmapPtr       pTile = NullPixmap;
    WindowPtr       pOrg = pWin;        /* whose origin the tile starts at */
    BoxPtr          pbox;
    int             nbox;

    if (what == PW_BACKGROUND)
    {
        WindowPtr p = pWin;

        while (p && p->backgroundState == ParentRelative)
            p = p->parent;
        if (!p)
            p = pWin;
        switch (p->backgroundState)
        {
        case None:
            if (p == pWin)
                return;
            pixel = (unsigned short) pWin->drawable.pScreen->blackPixel;
            break;
        case BackgroundPixel:
            pixel = (unsigned short) p->background.pixel;
            break;
        case BackgroundPixmap:
            pTile = p->background.pixmap;
            pOrg = p;
            break;
        default:
            pixel = (unsigned short) pWin->drawable.pScreen->blackPixel;
            break;
        }
    }
    else /* PW_BORDER */
    {
        if (pWin->borderIsPixel)
            pixel = (unsigned short) pWin->border.pixel;
        else
            pTile = pWin->border.pixmap;
    }

    nbox = REGION_NUM_RECTS(prgn);
    pbox = REGION_RECTS(prgn);

    if (pTile)
    {
        if (pTile->drawable.depth == ZZ9000_DEPTH && pTile->devPrivate.ptr &&
            pTile->drawable.width > 0 && pTile->drawable.height > 0)
        {
            tileBoxes(pRTG, pbox, nbox, pTile,
                      (int) pOrg->drawable.x, (int) pOrg->drawable.y);
            return;
        }
        pixel = (unsigned short) pWin->drawable.pScreen->blackPixel;
    }

    while (nbox--)
    {
        if (pbox->x1 < pbox->x2 && pbox->y1 < pbox->y2)
            blitFill(pRTG,
                     (unsigned short) pbox->x1,
                     (unsigned short) pbox->y1,
                     (unsigned short)(pbox->x2 - 1),
                     (unsigned short)(pbox->y2 - 1),
                     pixel);
        pbox++;
    }
    BLIT_FLUSH();
}

/*
** copyBoxesOrdered - copy the boxes of a YX-banded region, each from (box + dx,dy),
** in an order that never overwrites a source pixel before it is read: bands
** bottom-up when the copy moves down (dy < 0: the source lies above), boxes
** within a band right-to-left when it moves right (dx < 0) -- as cfb does and
** as ZZ9000CopyArea walks its bands.  copyVRAMBox orders rows and columns
** inside one box.  (CopyWindow used to choose the order from the sign of
** old - new the wrong way round, which smeared a partly covered window's
** contents into itself on every move.)
*/
static void
copyBoxesOrdered(pRTG, pbox, nbox, dx, dy)
rtgScreenPtr pRTG;
BoxPtr       pbox;
int          nbox, dx, dy;
{
    BoxPtr pboxEnd = pbox + nbox, pboxBand, pboxNext, first, last, b;
    int    bandsUp = (dy < 0), boxesLeft = (dx < 0);

    pboxBand = bandsUp ? pboxEnd : pbox;
    while (bandsUp ? (pboxBand > pbox) : (pboxBand < pboxEnd))
    {
        if (bandsUp)
        {
            last  = pboxBand - 1;
            first = last;
            while (first > pbox && (first - 1)->y1 == last->y1)
                first--;
            pboxNext = first;
        }
        else
        {
            first = pboxBand;
            last  = first;
            while (last + 1 < pboxEnd && (last + 1)->y1 == first->y1)
                last++;
            pboxNext = last + 1;
        }
        for (b = boxesLeft ? last : first;
             boxesLeft ? (b >= first) : (b <= last);
             b += boxesLeft ? -1 : 1)
            copyVRAMBox(pRTG, b->x1 + dx, b->y1 + dy,
                        b->x2 - b->x1, b->y2 - b->y1, b->x1, b->y1);
        pboxBand = pboxNext;
    }
}

/* ---------------------------------------------------------------------- */
/* ZZ9000CopyWindow                                                        */

/*
** ZZ9000CopyWindow - move window contents after the window is repositioned.
**
** Modelled on cfbCopyWindow:
**   1. Compute the destination region by intersecting prgnSrc (translated
**      to the new position) with pWin->borderClip.
**   2. Build a pptSrc[] array: source origin for each destination box.
**   3. Copy boxes in direction-aware order (bottom-to-top for downward
**      moves, reverse for rightward moves) to handle VRAM overlap.
*/
void
ZZ9000CopyWindow(pWin, ptOldOrg, prgnSrc)
WindowPtr   pWin;
DDXPointRec ptOldOrg;
RegionPtr   prgnSrc;
{
    rtgScreenPtr  pRTG = GetRTGScreen(pWin->drawable.pScreen);
    ScreenPtr     pScreen = pWin->drawable.pScreen;
    RegionPtr     prgnDst;
    int           nbox;
    int           dx, dy;

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;

    prgnDst = (*pScreen->RegionCreate)(NULL, 1);

    (*pScreen->TranslateRegion)(prgnSrc, -dx, -dy);
    (*pScreen->Intersect)(prgnDst, &pWin->borderClip, prgnSrc);

    nbox = REGION_NUM_RECTS(prgnDst);
    if (nbox == 0)
    {
        (*pScreen->RegionDestroy)(prgnDst);
        return;
    }

    copyBoxesOrdered(pRTG, REGION_RECTS(prgnDst), nbox, dx, dy);

    BLIT_FLUSH();
    (*pScreen->RegionDestroy)(prgnDst);
}

/* ---------------------------------------------------------------------- */
/* ZZ9000CopyArea                                                          */

/*
** ZZ9000CopyArea - copy a rectangular region between drawables.
**
** Fast path: both drawables are windows on the same screen -> direct VRAM
** copy.  Modelled on cfbBitBlt: the source rectangle is first clipped to
** what of the source window is actually visible (its clipList, or the
** area not clipped by children for IncludeInferiors), translated to the
** destination and clipped to the GC composite clip.  Only that region is
** copied -- the framebuffer under an obscured part of the source holds
** some other window's pixels, not the source's.  miHandleExposures then
** paints the destination background over the parts that could not be
** copied and, with graphics exposures on, returns the region the client
** must redraw (the GraphicsExpose events).
**
** All other cases (pixmap source or destination, or any raster op but
** GXcopy -- copyVRAMBox can only copy) fall back to miCopyArea, which uses
** GetSpans/SetSpans (correct but slower; SetSpans applies the alu).
*/
RegionPtr
ZZ9000CopyArea(pSrcDraw, pDstDraw, pGC, srcx, srcy, width, height, dstx, dsty)
DrawablePtr pSrcDraw;
DrawablePtr pDstDraw;
GCPtr       pGC;
int         srcx, srcy;
int         width, height;
int         dstx, dsty;
{
    ScreenPtr       pScreen;
    rtgScreenPtr    pRTG;
    RegionPtr       prgnSrcClip;
    RegionPtr       prgnExposed;
    RegionRec       rgnDst;
    BoxRec          box;
    BoxPtr          pbox, pboxBand, pboxNext, pboxEnd;
    int             freeSrcClip = FALSE;
    int             nbox, i;
    int             dx, dy;
    int             bandsUp, boxesLeft;

    /* Only a GXcopy window-to-window same-screen copy gets the fast VRAM blit */
    if (pSrcDraw->type != DRAWABLE_WINDOW ||
        pDstDraw->type != DRAWABLE_WINDOW ||
        pSrcDraw->pScreen != pDstDraw->pScreen ||
        pGC->alu != GXcopy)
    {
        return miCopyArea(pSrcDraw, pDstDraw, pGC,
                          srcx, srcy, width, height, dstx, dsty);
    }

    pScreen = pDstDraw->pScreen;
    pRTG    = GetRTGScreen(pScreen);

    if (!((WindowPtr) pDstDraw)->realized)
        return (RegionPtr) NULL;

    /*
    ** Fast path (P-5 fix 2a): the common scroll/copy of an unobscured window.
    ** When the source is clipped by its clipList (not IncludeInferiors), that
    ** clipList and the destination's composite clip are one box each, and
    ** the source rectangle lies wholly inside the source box, no part of the
    ** source is obscured: nothing to expose, one box to copy.  Returning
    ** NULL is protocol-correct -- ProcCopyArea hands it to SendGraphicsExpose,
    ** which sends the NoExpose a client with graphics exposures expects.
    ** Anything else takes the full region path below.
    */
    if (pGC->subWindowMode != IncludeInferiors &&
        REGION_NUM_RECTS(&((WindowPtr) pSrcDraw)->clipList) == 1 &&
        REGION_NUM_RECTS(rtgGCClip(pGC)) == 1)
    {
        BoxPtr sb = REGION_RECTS(&((WindowPtr) pSrcDraw)->clipList);
        BoxPtr db = REGION_RECTS(rtgGCClip(pGC));
        int    sx = pSrcDraw->x + srcx, sy = pSrcDraw->y + srcy;
        int    tx = pDstDraw->x + dstx, ty = pDstDraw->y + dsty;
        int    x1, y1, x2, y2;

        if (width > 0 && height > 0 &&
            sx >= sb->x1 && sy >= sb->y1 &&
            sx + width <= sb->x2 && sy + height <= sb->y2)
        {
            x1 = max(tx, db->x1);
            y1 = max(ty, db->y1);
            x2 = min(tx + width, db->x2);
            y2 = min(ty + height, db->y2);
            if (x1 < x2 && y1 < y2)
                copyVRAMBox(pRTG,
                            x1 + (sx - tx), y1 + (sy - ty),
                            x2 - x1, y2 - y1, x1, y1);
            BLIT_FLUSH();
            return (RegionPtr) NULL;
        }
    }

    /* What of the source is visible */
    if (pGC->subWindowMode == IncludeInferiors)
    {
        prgnSrcClip = NotClippedByChildren((WindowPtr) pSrcDraw);
        freeSrcClip = TRUE;
    }
    else
        prgnSrcClip = &((WindowPtr) pSrcDraw)->clipList;

    box.x1 = pSrcDraw->x + srcx;
    box.y1 = pSrcDraw->y + srcy;
    box.x2 = box.x1 + width;
    box.y2 = box.y1 + height;
    REGION_INIT(pScreen, &rgnDst, &box, 1);
    REGION_INTERSECT(pScreen, &rgnDst, &rgnDst, prgnSrcClip);

    /* Source -> destination offset; move the region, clip to the GC */
    dx = (pSrcDraw->x + srcx) - (pDstDraw->x + dstx);
    dy = (pSrcDraw->y + srcy) - (pDstDraw->y + dsty);
    REGION_TRANSLATE(pScreen, &rgnDst, -dx, -dy);
    REGION_INTERSECT(pScreen, &rgnDst, &rgnDst, rtgGCClip(pGC));

    /*
    ** Copy the boxes in an order that never overwrites a source box
    ** before it is read: bands bottom-up when the copy moves down
    ** (dy < 0: the source lies above), boxes within a band right-to-left
    ** when it moves right (dx < 0).  copyVRAMBox orders rows and columns
    ** inside one box the same way.
    */
    nbox    = REGION_NUM_RECTS(&rgnDst);
    pbox    = REGION_RECTS(&rgnDst);
    pboxEnd = pbox + nbox;
    bandsUp   = (dy < 0);
    boxesLeft = (dx < 0);

    if (nbox && width > 0 && height > 0)
    {
        if (!bandsUp && !boxesLeft)
        {
            for (i = 0; i < nbox; i++, pbox++)
                copyVRAMBox(pRTG,
                            pbox->x1 + dx, pbox->y1 + dy,
                            pbox->x2 - pbox->x1, pbox->y2 - pbox->y1,
                            pbox->x1, pbox->y1);
        }
        else
        {
            /* Walk the bands in the chosen order, each band's boxes in theirs */
            pboxBand = bandsUp ? pboxEnd : pbox;
            while (bandsUp ? (pboxBand > pbox) : (pboxBand < pboxEnd))
            {
                BoxPtr first, last, b;

                if (bandsUp)
                {
                    last  = pboxBand - 1;
                    first = last;
                    while (first > pbox && (first - 1)->y1 == last->y1)
                        first--;
                    pboxNext = first;
                }
                else
                {
                    first = pboxBand;
                    last  = first;
                    while (last + 1 < pboxEnd && (last + 1)->y1 == first->y1)
                        last++;
                    pboxNext = last + 1;
                }

                for (b = boxesLeft ? last : first;
                     boxesLeft ? (b >= first) : (b <= last);
                     b += boxesLeft ? -1 : 1)
                    copyVRAMBox(pRTG,
                                b->x1 + dx, b->y1 + dy,
                                b->x2 - b->x1, b->y2 - b->y1,
                                b->x1, b->y1);

                pboxBand = pboxNext;
            }
        }
    }

    BLIT_FLUSH();

    /*
    ** Always called (see mi/miexpose.c): it paints the destination
    ** background where the source was obscured, and returns the exposed
    ** region only when the GC has graphics exposures on.
    */
    prgnExposed = miHandleExposures(pSrcDraw, pDstDraw, pGC,
                                    srcx, srcy, width, height,
                                    dstx, dsty, 0L);

    REGION_UNINIT(pScreen, &rgnDst);
    if (freeSrcClip)
        REGION_DESTROY(pScreen, prgnSrcClip);
    return prgnExposed;
}

/* ---------------------------------------------------------------------- */
/* ZZ9K-SOFT3D: a 3D client's swap, through the window's clip list         */

/*
** A client that renders off-screen on the board (TinyGL on the Z3660's
** soft3D, tinygl-amix/src/zsoft3d.c) has its frame in VRAM rows below the
** visible screen.  Its swap asks the server to copy that rectangle into a
** window: only the visible parts of the window are written, one blitter
** copy per box of its clip list, with the software cursor taken down first
** (miSprite's SourceValidate; its BlockHandler puts it back), so menus,
** overlapping windows and the cursor survive.  The copies go through the
** driver's ZZ9KIOC_BLITX, which accepts off-screen rows (ZZ9KIOC_BLITS
** checks against the visible screen only); the extension is not offered
** by a kernel without it.
**
** Request (no reply), the client's copy is in zsoft3d.c:
**   CARD8 major, CARD8 minor (0 = Swap), CARD16 length (5),
**   CARD32 window, INT16 srcX, srcY (VRAM, screen stride), INT16 x, y
**   (window-relative), CARD16 width, height
*/
#include "dixstruct.h"
#include "extnsionst.h"

#define ZZ9KIOC_BLITX       (ZZ9K_IOC | 18)
struct zz9k_blitx {
    unsigned short op, sx, sy, dx, dy, width, height, pad;
    unsigned long  color;
};

typedef struct {
    CARD8  reqType;
    CARD8  s3dReqType;
    CARD16 length B16;
    CARD32 window B32;
    INT16  srcX B16, srcY B16;
    INT16  x B16, y B16;
    CARD16 width B16, height B16;
} xZZ9KSwapReq;
#define sz_xZZ9KSwapReq 20

static int s3dFd = -1;

static int
s3dBlitx(sx, sy, dx, dy, w, h)
int sx, sy, dx, dy, w, h;
{
    struct zz9k_blitx b;

    b.op = 1;
    b.sx = sx; b.sy = sy; b.dx = dx; b.dy = dy;
    b.width = w; b.height = h; b.pad = 0; b.color = 0;
    return ioctl(s3dFd, ZZ9KIOC_BLITX, &b);
}

static int
ProcZZ9KSoft3D(client)
ClientPtr client;
{
    REQUEST(xZZ9KSwapReq);
    WindowPtr pWin;
    ScreenPtr pScreen;
    RegionRec rgn;
    BoxRec    box;
    BoxPtr    pb;
    int       n, dx, dy;

    if (stuff->s3dReqType != 0)
        return BadRequest;
    REQUEST_SIZE_MATCH(xZZ9KSwapReq);
    pWin = LookupWindow(stuff->window, client);
    if (!pWin) {
        client->errorValue = stuff->window;
        return BadWindow;
    }
    if (!pWin->viewable || stuff->width == 0 || stuff->height == 0 ||
        stuff->srcX < 0 || stuff->srcY < 0)
        return client->noClientException;
    pScreen = pWin->drawable.pScreen;
    box.x1 = pWin->drawable.x + stuff->x;
    box.y1 = pWin->drawable.y + stuff->y;
    box.x2 = box.x1 + stuff->width;
    box.y2 = box.y1 + stuff->height;
    REGION_INIT(pScreen, &rgn, &box, 1);
    REGION_INTERSECT(pScreen, &rgn, &rgn, &pWin->clipList);
    n = REGION_NUM_RECTS(&rgn);
    if (n > 0) {
        if (pScreen->SourceValidate)
            (*pScreen->SourceValidate)((DrawablePtr) pWin, stuff->x, stuff->y,
                                       stuff->width, stuff->height);
        BLIT_FLUSH();
        dx = stuff->srcX - box.x1;
        dy = stuff->srcY - box.y1;
        for (pb = REGION_RECTS(&rgn); n > 0; n--, pb++)
            s3dBlitx(pb->x1 + dx, pb->y1 + dy, pb->x1, pb->y1,
                     pb->x2 - pb->x1, pb->y2 - pb->y1);
    }
    REGION_UNINIT(pScreen, &rgn);
    return client->noClientException;
}

static int
SProcZZ9KSoft3D(client)
ClientPtr client;
{
    return BadImplementation;       /* AMIX clients are all big-endian */
}

static void
ZZ9KSoft3DReset(ext)
ExtensionEntry *ext;
{
}

/* From ZZ9000ScreenInit: offer the request if the driver can copy for it. */
void
ZZ9000Soft3DInit(pRTG)
rtgScreenPtr pRTG;
{
    s3dFd = GetZZ9000Screen(pRTG)->fd;
    if (s3dBlitx(0, 0, 0, 0, 1, 1) < 0) {       /* one pixel onto itself */
        ErrorF("zz9000: no ZZ9KIOC_BLITX in this kernel; no ZZ9K-SOFT3D\n");
        return;
    }
    if (!AddExtension("ZZ9K-SOFT3D", 0, 0, ProcZZ9KSoft3D, SProcZZ9KSoft3D,
                      ZZ9KSoft3DReset, StandardMinorOpcode))
        ErrorF("zz9000: AddExtension ZZ9K-SOFT3D failed\n");
}
