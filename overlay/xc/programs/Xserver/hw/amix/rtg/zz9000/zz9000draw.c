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
/* CPU rectangle helpers. Hardware blit can be added after register bring-up. */

static void
blitFill(regBase, x1, y1, x2, y2, color)
pointer        regBase;
unsigned short x1, y1, x2, y2, color;
{
    unsigned short *base;
    unsigned short *p;
    int stride;
    int x, y;

    base = (unsigned short *)regBase;
    stride = ZZ9000_selected_mode->w;

    for (y = y1; y <= y2; ++y) {
        p = base + ((unsigned long)y * stride) + x1;
        for (x = x1; x <= x2; ++x)
            *p++ = color;
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
** copyVRAMBox - copy a screen rectangle in mapped framebuffer memory.
** blitter_base / blitter_base2 (0x40-0x46) supply the starting-row SDRAM
** word addresses; X/Y coordinate order controls copy direction so that
** overlapping moves are handled correctly by the hardware.
*/
static void
copyVRAMBox(regBase, fbBase, stride, srcx, srcy, w, h, dstx, dsty)
pointer         regBase;
unsigned short *fbBase;
int             stride;
int             srcx, srcy, w, h, dstx, dsty;
{
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
        for (col = col_start; col != col_end; col += col_step)
            dst[col] = src[col];
    }
}
/*
** Fill a horizontal run of w pixels with a solid 16-bit colour.
*/
static void
fillRun(dst, pixel, w)
unsigned short *dst;
unsigned short  pixel;
int             w;
{
    while (w-- > 0)
        *dst++ = pixel;
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

    for (i = 0; i < nspans; i++)
    {
        w = clippedWidths[i];
        if (w <= 0)
            continue;
        p = base + clippedPts[i].y * stride + clippedPts[i].x;

        switch (alu)
        {
        case GXcopy:         while (w-->0)  *p++ = pixel;              break;
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
        default:             while (w-->0)  *p++ = pixel;              break;
        }
    }

    DEALLOCATE_LOCAL(clippedWidths);
    DEALLOCATE_LOCAL(clippedPts);
}

/* ---------------------------------------------------------------------- */
/* ZZ9000SetSpans                                                          */

/*
** ZZ9000SetSpans - write packed 16-bit pixel data to the framebuffer.
**
** Each source scanline is padded according to PixmapBytePad(), as required
** by the DDX SetSpans contract.  For each input span we clip each clip-box
** intersection and copy the appropriate slice of the source.
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

            if (cx1 < cx2)
                memcpy((char *)(base + ppt->y * stride + cx1),
                       (char *)(psrc + (cx1 - ppt->x)),
                       (size_t)((cx2 - cx1) * 2));
            pbox++;
        }
        psrcLine += srcBytes;
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
        memcpy(dst,
               (char *)(base + ppt->y * stride + ppt->x),
               (size_t)(w * 2));
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
*/
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
                if (useHW)
                {
                    blitFill((pointer)pRTG->fbBase,
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
                    for (y = cy1; y < cy1 + h; y++)
                        fillRun(base + y * stride + cx1, pixel, w);
                }
            }
            pbox++;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* ZZ9000PaintWindow                                                       */

/*
** ZZ9000PaintWindow - paint window background (PW_BACKGROUND) or border
** (PW_BORDER).
**
** Fast path: solid pixel -> direct VRAM fill.
** Pixmap backgrounds are not yet tiled; the window is left as-is
** (garbage on first expose).  TODO: implement tile/stipple painting.
*/
void
ZZ9000PaintWindow(pWin, prgn, what)
WindowPtr pWin;
RegionPtr prgn;
int       what;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pWin->drawable.pScreen);
    unsigned short  pixel;
    BoxPtr          pbox;
    int             nbox;

    if (what == PW_BACKGROUND)
    {
        switch (pWin->backgroundState)
        {
        case None:
            return;
        case BackgroundPixel:
            pixel = (unsigned short) pWin->background.pixel;
            break;
        case ParentRelative:
            /* Walk up to find the nearest solid background */
            {
                WindowPtr p = pWin->parent;
                while (p && p->backgroundState == ParentRelative)
                    p = p->parent;
                if (p && p->backgroundState == BackgroundPixel)
                    pixel = (unsigned short) p->background.pixel;
                else
                    pixel = (unsigned short) pWin->drawable.pScreen->blackPixel;
            }
            break;
        default:
            /* BackgroundPixmap: read first pixel from tile for solid approx. */
            {
                PixmapPtr   pTile  = pWin->background.pixmap;
                unsigned short *tp = (unsigned short *) pTile->devPrivate.ptr;
                pixel = (pTile && pTile->drawable.depth == ZZ9000_DEPTH && tp)
                        ? *tp
                        : (unsigned short) pWin->drawable.pScreen->blackPixel;
            }
            break;
        }
    }
    else /* PW_BORDER */
    {
        if (pWin->borderIsPixel)
            pixel = (unsigned short) pWin->border.pixel;
        else
        {
            /* Pixmap border: read first pixel for solid approx. */
            PixmapPtr   pTile  = pWin->border.pixmap;
            unsigned short *tp = (unsigned short *) pTile->devPrivate.ptr;
            pixel = (pTile && pTile->drawable.depth == ZZ9000_DEPTH && tp)
                    ? *tp
                    : (unsigned short) pWin->drawable.pScreen->blackPixel;
        }
    }

    nbox = REGION_NUM_RECTS(prgn);
    pbox = REGION_RECTS(prgn);

    {
        pointer regBase = (pointer)pRTG->fbBase;
        while (nbox--)
        {
            if (pbox->x1 < pbox->x2 && pbox->y1 < pbox->y2)
                blitFill(regBase,
                         (unsigned short) pbox->x1,
                         (unsigned short) pbox->y1,
                         (unsigned short)(pbox->x2 - 1),
                         (unsigned short)(pbox->y2 - 1),
                         pixel);
            pbox++;
        }
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
    DDXPointPtr   pptSrc, ppt;
    BoxPtr        pbox;
    int           nbox, i;
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

    pptSrc = (DDXPointPtr) ALLOCATE_LOCAL(nbox * sizeof(DDXPointRec));
    if (!pptSrc)
    {
        (*pScreen->RegionDestroy)(prgnDst);
        return;
    }

    pbox = REGION_RECTS(prgnDst);
    ppt  = pptSrc;
    for (i = 0; i < nbox; i++, ppt++, pbox++)
    {
        ppt->x = pbox->x1 + dx;
        ppt->y = pbox->y1 + dy;
    }

    pbox = REGION_RECTS(prgnDst);
    ppt  = pptSrc;

    if (dy > 0 || (dy == 0 && dx > 0))
    {
        /* Moving down or right: process boxes in reverse order */
        pbox += nbox - 1;
        ppt  += nbox - 1;
        for (i = 0; i < nbox; i++, pbox--, ppt--)
        {
            copyVRAMBox(pRTG->frameBase, pRTG->fbBase, ZZ9000_STRIDE(pRTG),
                        ppt->x,  ppt->y,
                        pbox->x2 - pbox->x1,
                        pbox->y2 - pbox->y1,
                        pbox->x1, pbox->y1);
        }
    }
    else
    {
        for (i = 0; i < nbox; i++, pbox++, ppt++)
        {
            copyVRAMBox(pRTG->frameBase, pRTG->fbBase, ZZ9000_STRIDE(pRTG),
                        ppt->x,  ppt->y,
                        pbox->x2 - pbox->x1,
                        pbox->y2 - pbox->y1,
                        pbox->x1, pbox->y1);
        }
    }

    DEALLOCATE_LOCAL(pptSrc);
    (*pScreen->RegionDestroy)(prgnDst);
}

/* ---------------------------------------------------------------------- */
/* ZZ9000CopyArea                                                          */

/*
** ZZ9000CopyArea - copy a rectangular region between drawables.
**
** Fast path: both drawables are windows on the same screen -> direct VRAM
** copy clipped against the GC composite clip.
**
** All other cases (pixmap source or destination) fall back to miCopyArea,
** which uses GetSpans/SetSpans (correct but slower).
**
** Returns NULL (no backing-store damage region).
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
    rtgScreenPtr   pRTG;
    unsigned short *fbBase;
    RegionPtr       clip;
    BoxPtr          pbox;
    int             nbox;
    int             absDstX, absDstY;
    int             absSrcX, absSrcY;
    int             cx1, cy1, cx2, cy2;
    int             bw, bh;
    int             bsrcx, bsrcy;
    int             dx_move, dy_move;
    int             i;

    /* Only the window-to-window same-screen path gets the fast VRAM blit */
    if (pSrcDraw->type != DRAWABLE_WINDOW ||
        pDstDraw->type != DRAWABLE_WINDOW ||
        pSrcDraw->pScreen != pDstDraw->pScreen)
    {
        return miCopyArea(pSrcDraw, pDstDraw, pGC,
                          srcx, srcy, width, height, dstx, dsty);
    }

    pRTG   = GetRTGScreen(pDstDraw->pScreen);
    fbBase = pRTG->fbBase;
    clip   = rtgGCClip(pGC);

    /* Absolute destination rectangle */
    absDstX = pDstDraw->x + dstx;
    absDstY = pDstDraw->y + dsty;

    /* Absolute source origin */
    absSrcX = pSrcDraw->x + srcx;
    absSrcY = pSrcDraw->y + srcy;

    /* Delta from destination to source (source = destination + delta) */
    dx_move = absDstX - absSrcX;
    dy_move = absDstY - absSrcY;

    nbox = REGION_NUM_RECTS(clip);
    pbox = REGION_RECTS(clip);

    /*
    ** Process clip boxes in direction-aware order to handle VRAM overlap.
    ** Boxes from REGION_RECTS are sorted top-to-bottom, left-to-right.
    */
    if (dy_move > 0 || (dy_move == 0 && dx_move > 0))
        pbox += nbox - 1;   /* start from last box */

    for (i = 0; i < nbox; i++)
    {
        cx1 = max(absDstX,          pbox->x1);
        cy1 = max(absDstY,          pbox->y1);
        cx2 = min(absDstX + width,  pbox->x2);
        cy2 = min(absDstY + height, pbox->y2);

        if (cx1 < cx2 && cy1 < cy2)
        {
            bsrcx = absSrcX + (cx1 - absDstX);
            bsrcy = absSrcY + (cy1 - absDstY);
            bw    = cx2 - cx1;
            bh    = cy2 - cy1;

            copyVRAMBox(pRTG->frameBase, fbBase, ZZ9000_STRIDE(pRTG), bsrcx, bsrcy, bw, bh, cx1, cy1);
        }

        if (dy_move > 0 || (dy_move == 0 && dx_move > 0))
            pbox--;
        else
            pbox++;
    }

    return (RegionPtr) NULL;
}
