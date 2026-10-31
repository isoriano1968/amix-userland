/*
** ZZ9000screen.c - ZZ9000 ScreenRec initialisation and GC management.
**
** ZZ9000ScreenInit() builds the full ScreenRec function vector.  It is
** called from rtgInit.c:rtgScreenInit() after ZZ9000InitHW() has opened
** the device and written the mode registers.
**
** GC lifecycle functions (CreateGC, ValidateGC, DestroyGC, ChangeClip,
** DestroyClip, CopyClip) are also here, modelled on dmi/tiggc.c but
** with depth=16 and no TIGA-specific tile/stipple hardware.
**
** Drawing ops installed in ValidateGC:
**   FillSolid        -> ZZ9000FillSpans / ZZ9000SolidRect  (ZZ9000draw.c)
**   Tile/Stipple     -> ZZ9000PatternSpans, rectangles through mi
**   CopyArea         -> ZZ9000CopyArea                     (ZZ9000draw.c)
**   Lines/poly/arcs  -> mi layer (miZeroLine, miPolyArc ...)
**   Text             -> mi layer (miPolyText8, miImageText8 ...)
*/

#include <stdio.h>
#include "X.h"
#include "Xmd.h"
#include "scrnintstr.h"
#include "regionstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "resource.h"
#include "colormap.h"
#include "colormapst.h"
#include "servermd.h"
#include "mi.h"
#include "mfb.h"
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

/* ------------------------------------------------------------------ */
/* External mi functions used below */

extern RegionPtr miCopyPlane();
extern RegionPtr miCopyArea();
extern void      miPolyPoint();
extern Bool      miModifyPixmapHeader();

/* ------------------------------------------------------------------ */
/* Forward declarations for functions defined later in this file */

void ZZ9000ValidateGC();
void ZZ9000DestroyGC();
void ZZ9000ChangeClip();
void ZZ9000DestroyClip();
void ZZ9000CopyClip();
static void ZZ9000ChangeGC();
static void ZZ9000CopyGC();
static void ZZ9000pixValidateGC();

/* ------------------------------------------------------------------ */
/* Functions implemented in other ZZ9000 source files */

/* ZZ9000draw.c */
extern void      ZZ9000FillSpans();
extern void      ZZ9000PatternSpans();
extern void      ZZ9000SetSpans();
extern void      ZZ9000PutImage();
extern RegionPtr ZZ9000CopyArea();
extern void      ZZ9000CopyWindow();
extern void      ZZ9000PaintWindow();
extern void      ZZ9000SolidRect();

/* ZZ9000win.c */
extern Bool ZZ9000CreateWindow();
extern Bool ZZ9000DestroyWindow();
extern Bool ZZ9000PositionWindow();
extern Bool ZZ9000ChangeWindowAttributes();
extern Bool ZZ9000RealizeWindow();
extern Bool ZZ9000UnrealizeWindow();

/* ZZ9000cmap.c */
extern Bool ZZ9000CreateColormap();
extern void ZZ9000DestroyColormap();
extern void ZZ9000InstallColormap();
extern void ZZ9000UninstallColormap();
extern int  ZZ9000ListInstalledColormaps();
extern void ZZ9000StoreColors();
extern void ZZ9000ResolveColor();
extern Bool ZZ9000CreateDefColormap();

/* ZZ9000pix.c */
extern PixmapPtr ZZ9000CreatePixmap();
extern Bool      ZZ9000DestroyPixmap();
extern Bool      ZZ9000RealizeFont();
extern Bool      ZZ9000UnrealizeFont();

/* rtgInit.c */
extern int rtgGCPrivateIndex;

/* ------------------------------------------------------------------ */
/* GCFuncs and GCOps default tables                                   */

static GCFuncs ZZ9000gcFuncs =
{
    ZZ9000ValidateGC,
    ZZ9000ChangeGC,
    ZZ9000CopyGC,
    ZZ9000DestroyGC,
    ZZ9000ChangeClip,
    ZZ9000DestroyClip,
    ZZ9000CopyClip,
};

/*
** Default ops installed at CreateGC time.  ValidateGC may replace
** individual slots based on fill style, line width, etc.
*/
static GCOps ZZ9000gcOps =
{
    ZZ9000FillSpans,    /* FillSpans   */
    ZZ9000SetSpans,     /* SetSpans    */
    ZZ9000PutImage,     /* PutImage    - 16-bit ZPixmap direct, else mi */
    ZZ9000CopyArea,     /* CopyArea    */
    miCopyPlane,        /* CopyPlane   */
    miPolyPoint,        /* PolyPoint   */
    miZeroLine,         /* Polylines   */
    miPolySegment,      /* PolySegment */
    miPolyRectangle,    /* PolyRectangle */
    miPolyArc,          /* PolyArc     */
    miFillPolygon,      /* FillPolygon */
    ZZ9000SolidRect,    /* PolyFillRect */
    miPolyFillArc,      /* PolyFillArc */
    miPolyText8,        /* PolyText8   */
    miPolyText16,       /* PolyText16  */
    miImageText8,       /* ImageText8  */
    miImageText16,      /* ImageText16 */
    miImageGlyphBlt,    /* ImageGlyphBlt */
    miPolyGlyphBlt,     /* PolyGlyphBlt */
    miPushPixels        /* PushPixels  */
#ifdef NEED_LINEHELPER
    , miMiter           /* LineHelper  */
#endif
};

/* ------------------------------------------------------------------ */
/* Internal GCFuncs helpers (no-ops; state changes handled in Validate) */

static void
ZZ9000ChangeGC(pGC, mask)
GCPtr  pGC;
BITS32 mask;
{
}

static void
ZZ9000CopyGC(pGCSrc, changes, pGCDst)
GCPtr pGCSrc;
Mask  changes;
GCPtr pGCDst;
{
}

/* ------------------------------------------------------------------ */
/* GC lifecycle                                                        */

/*
** ZZ9000CreateGC - allocate GCFuncs, GCOps and private clip region.
*/
Bool
ZZ9000CreateGC(pGC)
GCPtr pGC;
{
    RTGGCPRIV *pPriv;

#ifdef ZZ9000_DEBUG
    ErrorF("ZZ9000CreateGC: depth=%d\n", pGC->depth);
#endif

    /* Delegate 1-bit GCs to mfb - same pattern as cfb/cfbgc.c line 241.
       mfbAllocatePrivates() in rtgScreenInit ensures the mfb GC private
       slot exists before any GC is created. */
    if (pGC->depth == 1)
        return mfbCreateGC(pGC);

    if (pGC->depth != ZZ9000_DEPTH)
    {
        ErrorF("ZZ9000CreateGC: unsupported depth %d\n", pGC->depth);
        return FALSE;
    }

#ifdef ZZ9000_DEBUG
    ErrorF("ZZ9000CreateGC: pPriv=%p\n", pGC->devPrivates[rtgGCPrivateIndex].ptr);
#endif

    pGC->funcs = (GCFuncs *) xalloc(sizeof(GCFuncs));
    if (!pGC->funcs)
        return FALSE;

    pGC->ops = (GCOps *) xalloc(sizeof(GCOps));
    if (!pGC->ops)
    {
        xfree((pointer) pGC->funcs);
        pGC->funcs = NULL;
        return FALSE;
    }

    *pGC->funcs = ZZ9000gcFuncs;
    *pGC->ops   = ZZ9000gcOps;

    pGC->clientClip     = NULL;
    pGC->clientClipType = CT_NONE;
    pGC->miTranslate    = TRUE;
    pGC->fgPixel        = pGC->pScreen->blackPixel;
    pGC->bgPixel        = pGC->pScreen->whitePixel;
    pGC->alu            = GXcopy;
    pGC->planemask      = ~0;
    pGC->lineWidth      = 0;
    pGC->lineStyle      = LineSolid;
    pGC->capStyle       = CapButt;
    pGC->fillStyle      = FillSolid;
    pGC->joinStyle      = JoinMiter;
    pGC->fillRule       = EvenOddRule;
    pGC->arcMode        = ArcPieSlice;
    pGC->patOrg.x   = pGC->patOrg.y   = 0;
    pGC->clipOrg.x  = pGC->clipOrg.y  = 0;
    pGC->lastWinOrg.x = pGC->lastWinOrg.y = 0;

    pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;
    pPriv->clip = (*pGC->pScreen->RegionCreate)(NULL, 1);

    return TRUE;
}

/*
** ZZ9000DestroyGC - free GCFuncs, GCOps and private clip region.
*/
void
ZZ9000DestroyGC(pGC)
GCPtr pGC;
{
    RTGGCPRIV *pPriv;

    xfree((pointer) pGC->funcs);
    pGC->funcs = NULL;
    xfree((pointer) pGC->ops);
    pGC->ops = NULL;

    pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;
    if (pPriv->clip)
        (*pGC->pScreen->RegionDestroy)(pPriv->clip);
}

/* ------------------------------------------------------------------ */
/* ValidateGC                                                          */

/*
** ZZ9000pixValidateGC - GC for drawing into an off-screen pixmap.
**
** All ops fall back to mi.  ZZ9000FillSpans handles the solid-fill
** case in software (direct pixel writes into host-allocated pixmap data).
*/
static void
ZZ9000pixValidateGC(pGC, change, pDraw)
GCPtr         pGC;
unsigned long change;
DrawablePtr   pDraw;
{
    RTGGCPRIV *pPriv;
    BoxRec     tbox;
    int        win_moved;

    if ((change & (GCClipXOrigin | GCClipYOrigin | GCClipMask)) ||
        (change & GCSubwindowMode) ||
        (pDraw->serialNumber != (pGC->serialNumber & DRAWABLE_SERIAL_BITS)))
    {
        pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;
        win_moved = (pGC->lastWinOrg.x != pDraw->x ||
                     pGC->lastWinOrg.y != pDraw->y);

        if (pGC->clientClipType != CT_NONE && win_moved)
        {
            (*pGC->pScreen->RegionCopy)((RegionPtr) pPriv->clip,
                                         (RegionPtr) pGC->clientClip);
            (*pGC->pScreen->TranslateRegion)(pPriv->clip,
                pGC->lastWinOrg.x + pGC->clipOrg.x,
                pGC->lastWinOrg.y + pGC->clipOrg.y);
        }
        else if (pGC->clientClipType == CT_NONE)
        {
            (*pGC->pScreen->RegionDestroy)(pPriv->clip);
            tbox.x1 = 0;           tbox.y1 = 0;
            tbox.x2 = pDraw->width; tbox.y2 = pDraw->height;
            pPriv->clip = (*pGC->pScreen->RegionCreate)(&tbox, 1);
        }
    }

    /* solid fills direct; tiles and stipples through ZZ9000PatternSpans */
    pGC->ops->FillSpans     = (pGC->fillStyle == FillSolid) ? ZZ9000FillSpans
                                                            : ZZ9000PatternSpans;
    pGC->ops->SetSpans      = ZZ9000SetSpans;
    pGC->ops->PutImage      = ZZ9000PutImage;
    pGC->ops->CopyArea      = ZZ9000CopyArea;
    pGC->ops->CopyPlane     = miCopyPlane;
    pGC->ops->PolyPoint     = miPolyPoint;
    pGC->ops->Polylines     = miZeroLine;
    pGC->ops->PolySegment   = miPolySegment;
    pGC->ops->PolyRectangle = miPolyRectangle;
    pGC->ops->PolyArc       = miPolyArc;
    pGC->ops->FillPolygon   = miFillPolygon;
    pGC->ops->PolyFillRect  = (pGC->fillStyle == FillSolid) ? ZZ9000SolidRect
                                                            : miPolyFillRect;
    pGC->ops->PolyFillArc   = miPolyFillArc;
    pGC->ops->PolyText8     = miPolyText8;
    pGC->ops->PolyText16    = miPolyText16;
    pGC->ops->ImageText8    = miImageText8;
    pGC->ops->ImageText16   = miImageText16;
    pGC->ops->ImageGlyphBlt = miImageGlyphBlt;
    pGC->ops->PolyGlyphBlt  = miPolyGlyphBlt;
    pGC->ops->PushPixels    = miPushPixels;
#ifdef NEED_LINEHELPER
    pGC->ops->LineHelper    = miMiter;
#endif
}

/*
** ZZ9000ValidateGC - update clip region and select drawing ops.
**
** Called by DIX before any drawing sequence on a new window/GC pair.
** Keeps the cached clip region in GC private up to date, then installs
** the appropriate ops:
**
**   FillSolid  -> ZZ9000FillSpans / ZZ9000SolidRect
**   FillTiled  -> ZZ9000PatternSpans (rectangles and polygons through mi)
**   Stipple    -> ZZ9000PatternSpans
**   Lines      -> miZeroLine (zero-width) / miWideLine (wide)
**   Everything else -> mi
*/
void
ZZ9000ValidateGC(pGC, change, pDraw)
GCPtr         pGC;
unsigned long change;
DrawablePtr   pDraw;
{
    RTGGCPRIV *pPriv;
    WindowPtr  pWin;
    int        win_moved;
    RegionPtr  reg;

    if (pDraw->type == DRAWABLE_PIXMAP)
    {
        ZZ9000pixValidateGC(pGC, ~0UL, pDraw);
        return;
    }

    pWin = (WindowPtr) pDraw;
    win_moved = (pGC->lastWinOrg.x != pDraw->x ||
                 pGC->lastWinOrg.y != pDraw->y);
    if (win_moved)
    {
        pGC->lastWinOrg.x = pDraw->x;
        pGC->lastWinOrg.y = pDraw->y;
    }

    if (change & (GCClipXOrigin | GCClipYOrigin | GCClipMask) ||
        change & GCSubwindowMode ||
        pDraw->serialNumber != (pGC->serialNumber & DRAWABLE_SERIAL_BITS) ||
        win_moved)
    {
        pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;

        if (pGC->clientClipType != CT_NONE &&
            (change & (GCClipXOrigin | GCClipYOrigin | GCClipMask) || win_moved))
        {
            (*pGC->pScreen->RegionCopy)((RegionPtr) pPriv->clip,
                                         (RegionPtr) pGC->clientClip);
            (*pGC->pScreen->TranslateRegion)(pPriv->clip,
                pGC->lastWinOrg.x + pGC->clipOrg.x,
                pGC->lastWinOrg.y + pGC->clipOrg.y);
        }
        else
        {
            (*pGC->pScreen->RegionCopy)((RegionPtr) pPriv->clip,
                                         &pWin->winSize);
        }

        reg = (pGC->subWindowMode == IncludeInferiors)
              ? NotClippedByChildren(pWin)
              : &pWin->clipList;
        (*pGC->pScreen->Intersect)(pPriv->clip, pPriv->clip, reg);
    }

    /* Line ops */
    if (pGC->lineStyle == LineSolid)
    {
        if (pGC->lineWidth)
        {
            pGC->ops->Polylines   = miWideLine;
            pGC->ops->PolySegment = miPolySegment;
        }
        else
        {
            pGC->ops->Polylines   = miZeroLine;
            pGC->ops->PolySegment = miPolySegment;
        }
    }
    else
    {
        pGC->ops->Polylines   = pGC->lineWidth ? miWideDash : miZeroLine;
        pGC->ops->PolySegment = miPolySegment;
    }

#ifdef NEED_LINEHELPER
    pGC->ops->LineHelper = (pGC->joinStyle == JoinMiter) ? miMiter : miNotMiter;
#endif
    pGC->ops->SetSpans   = ZZ9000SetSpans;

    /* Fill ops: solid direct, tile/stipple spans through ZZ9000PatternSpans */
    switch (pGC->fillStyle)
    {
    case FillSolid:
        pGC->ops->FillSpans    = ZZ9000FillSpans;
        pGC->ops->FillPolygon  = miFillPolygon;
        pGC->ops->PolyFillRect = ZZ9000SolidRect;
        pGC->ops->PushPixels   = miPushPixels;
        pGC->ops->PolyText8    = miPolyText8;
        break;

    case FillTiled:
        pGC->ops->FillSpans    = ZZ9000PatternSpans;
        pGC->ops->FillPolygon  = miFillPolygon;
        pGC->ops->PolyFillRect = miPolyFillRect;
        pGC->ops->PushPixels   = miPushPixels;
        pGC->ops->PolyText8    = miPolyText8;
        break;

    case FillStippled:
    case FillOpaqueStippled:
        pGC->ops->FillSpans    = ZZ9000PatternSpans;
        pGC->ops->FillPolygon  = miFillPolygon;
        pGC->ops->PolyFillRect = miPolyFillRect;
        pGC->ops->PushPixels   = miPushPixels;
        pGC->ops->PolyText8    = miPolyText8;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Clip region management                                              */

/*
** ZZ9000ChangeClip - set a new client clip region on a GC.
*/
void
ZZ9000ChangeClip(pGC, type, pval, nrects)
GCPtr   pGC;
int     type;
pointer pval;
int     nrects;
{
    ZZ9000DestroyClip(pGC);

    if (type == CT_PIXMAP)
    {
        pGC->clientClip = (pointer) mfbPixmapToRegion(pval);
        (*pGC->pScreen->DestroyPixmap)(pval);
    }
    else if (type == CT_REGION)
    {
        pGC->clientClip = pval;
    }
    else if (type != CT_NONE)
    {
        pGC->clientClip = (pointer)
            (*pGC->pScreen->RectsToRegion)(nrects, (xRectangle *) pval, type);
        xfree(pval);
    }
    else
    {
        pGC->clientClipType = CT_NONE;
        pGC->stateChanges  |= GCClipMask;
    }

    pGC->clientClipType =
        (type != CT_NONE && pGC->clientClip) ? CT_REGION : CT_NONE;
    pGC->stateChanges |= GCClipMask;
}

/*
** ZZ9000DestroyClip - free the current client clip region.
*/
void
ZZ9000DestroyClip(pGC)
GCPtr pGC;
{
    if (pGC->clientClipType == CT_PIXMAP)
        (*pGC->pScreen->DestroyPixmap)(pGC->clientClip);
    else if (pGC->clientClipType != CT_NONE)
        (*pGC->pScreen->RegionDestroy)(pGC->clientClip);

    pGC->clientClipType = CT_NONE;
    pGC->clientClip     = NULL;
}

/*
** ZZ9000CopyClip - copy the client clip region from src GC to dst GC.
*/
void
ZZ9000CopyClip(dst, src)
GCPtr dst;
GCPtr src;
{
    RegionPtr rgnCopy;

    if (src->clientClipType == CT_REGION)
    {
        rgnCopy = (*src->pScreen->RegionCreate)(NULL, 1);
        (*src->pScreen->RegionCopy)(rgnCopy, (RegionPtr) src->clientClip);
        ZZ9000ChangeClip(dst, CT_REGION, (pointer) rgnCopy, 0);
        return;
    }
    if (src->clientClipType == CT_PIXMAP)
        ((PixmapPtr) src->clientClip)->refcnt++;
    ZZ9000ChangeClip(dst, src->clientClipType, src->clientClip, 0);
}

/* ------------------------------------------------------------------ */
/* Screen-level functions                                              */

/*
** ZZ9000QueryBestSize - return an acceptable size for cursors, tiles, stipples.
**
** Software cursor can be any size; we cap at 64x64 to match TIGA.
** Tile and stipple: no hardware constraint - return requested size.
*/
void
ZZ9000QueryBestSize(kind, W, H, pScreen)
int     kind;
CARD16 *W;
CARD16 *H;
ScreenPtr pScreen;
{
    switch (kind)
    {
    case CursorShape:
        if (*W > 64) *W = 64;
        if (*H > 64) *H = 64;
        break;

    case TileShape:
    case StippleShape:
        /* No hardware constraint on tile/stipple size */
        break;

    default:
        break;
    }
}

/*
** ZZ9000CloseScreen - minimal hook; hardware cleanup is in rtgCloseScreen.
**
** rtgInit.c:rtgCloseScreen() chains to this, then calls ZZ9000CloseHW().
** This function returns TRUE so the DIX chain continues.
*/
Bool
ZZ9000CloseScreen(i, pScreen)
int       i;
ScreenPtr pScreen;
{
    return TRUE;
}

/*
** ZZ9000DrawGuarantee - no backing store; nothing to do.
**
** Called by miDCMakeGC before drawing the software cursor.
** With backingStoreSupport = NotUseful, there is no backing store to
** guarantee, so this is a no-op.
*/
void
ZZ9000DrawGuarantee(pWin, pGC, guarantee)
WindowPtr pWin;
GCPtr     pGC;
int       guarantee;
{
}

/* ------------------------------------------------------------------ */
/* Screen initialisation                                               */

/*
** ZZ9000ScreenInit - build the complete ScreenRec function vector.
**
** Called from rtgInit.c:rtgScreenInit() after ZZ9000InitHW() has set
** pRTG->fbBase.  pVisual and pDepth are the static TrueColor records
** initialised in rtgInit.c:rtgCreate().
**
** Owns no persistent state beyond what it installs in pScreen.
*/
Bool
ZZ9000ScreenInit(pScreen, pRTG, dpix, dpiy, pVisual, pDepth)
ScreenPtr    pScreen;
rtgScreenPtr pRTG;
int          dpix, dpiy;
VisualRec   *pVisual;
DepthRec    *pDepth;
{
    /* Screen geometry */
    pScreen->width    = pRTG->width;
    pScreen->height   = pRTG->height;
    pScreen->mmWidth  = (pRTG->width  * 254) / (dpix * 10);
    pScreen->mmHeight = (pRTG->height * 254) / (dpiy * 10);

    /* Visuals and depths */
    pScreen->numDepths     = 1;
    pScreen->allowedDepths = pDepth;
    pScreen->rootDepth     = ZZ9000_DEPTH;
    pScreen->rootVisual    = pVisual->vid;
    pScreen->defColormap   = (Colormap) FakeClientID(0);
    pScreen->minInstalledCmaps = 1;
    pScreen->maxInstalledCmaps = 1;
    pScreen->numVisuals    = 1;
    pScreen->visuals       = pVisual;

    pScreen->backingStoreSupport = NotUseful;
    pScreen->saveUnderSupport    = NotUseful;
    pScreen->DrawGuarantee       = ZZ9000DrawGuarantee;
    pScreen->PostValidateTree    = (void (*)()) 0;
    pScreen->PointerNonInterestBox = (void (*)()) 0;
    pScreen->ClipNotify          = (void (*)()) 0;

    pScreen->SaveDoomedAreas       = (void (*)()) 0;
    pScreen->RestoreAreas           = (RegionPtr (*)()) 0;
    pScreen->ExposeCopy             = (void (*)()) 0;
    pScreen->TranslateBackingStore  = (RegionPtr (*)()) 0;
    pScreen->ClearBackingStore      = (RegionPtr (*)()) 0;

    pScreen->blackPixel = 0x0000;   /* RGB565 black */
    pScreen->whitePixel = 0xFFFF;   /* RGB565 white */

    /* Window management - thin hooks; real work done by DIX/mi */
    pScreen->CreateWindow         = ZZ9000CreateWindow;
    pScreen->DestroyWindow        = ZZ9000DestroyWindow;
    pScreen->PositionWindow       = ZZ9000PositionWindow;
    pScreen->ChangeWindowAttributes = ZZ9000ChangeWindowAttributes;
    pScreen->RealizeWindow        = ZZ9000RealizeWindow;
    pScreen->UnrealizeWindow      = ZZ9000UnrealizeWindow;

    /* Font management - no hardware fonts */
    pScreen->RealizeFont   = ZZ9000RealizeFont;
    pScreen->UnrealizeFont = ZZ9000UnrealizeFont;

    /* Screen lifecycle */
    pScreen->CloseScreen      = ZZ9000CloseScreen;
    pScreen->QueryBestSize    = ZZ9000QueryBestSize;
    pScreen->SaveScreen       = (Bool (*)()) NULL; /* installed by rtgInit */
    pScreen->SourceValidate   = (void (*)()) NULL;

    /* Pixel transfer - mi handles format conversion */
    pScreen->GetImage  = ZZ9000GetImage;
    pScreen->GetSpans  = ZZ9000GetSpans;

    /* GC */
    pScreen->CreateGC      = ZZ9000CreateGC;

    /* Pixmaps */
    pScreen->CreatePixmap       = ZZ9000CreatePixmap;
    pScreen->DestroyPixmap      = ZZ9000DestroyPixmap;
    pScreen->ModifyPixmapHeader = miModifyPixmapHeader;

    /* Validation */
    pScreen->ValidateTree = miValidateTree;

    /* Colormap - TrueColor: no CLUT; most entries are no-ops */
    pScreen->InstallColormap        = ZZ9000InstallColormap;
    pScreen->UninstallColormap      = ZZ9000UninstallColormap;
    pScreen->ListInstalledColormaps = ZZ9000ListInstalledColormaps;
    pScreen->StoreColors            = ZZ9000StoreColors;
    pScreen->ResolveColor           = ZZ9000ResolveColor;
    pScreen->CreateColormap         = ZZ9000CreateColormap;
    pScreen->DestroyColormap        = ZZ9000DestroyColormap;

    /* Region operations - pure mi */
    pScreen->RegionCreate    = miRegionCreate;
    pScreen->RegionInit      = miRegionInit;
    pScreen->RegionCopy      = miRegionCopy;
    pScreen->RegionDestroy   = miRegionDestroy;
    pScreen->RegionUninit    = miRegionUninit;
    pScreen->Intersect       = miIntersect;
    pScreen->Inverse         = miInverse;
    pScreen->Union           = miUnion;
    pScreen->Subtract        = miSubtract;
    pScreen->RegionReset     = miRegionReset;
    pScreen->TranslateRegion = miTranslateRegion;
    pScreen->RectIn          = miRectIn;
    pScreen->PointInRegion   = miPointInRegion;
    pScreen->RegionNotEmpty  = miRegionNotEmpty;
    pScreen->RegionEmpty     = miRegionEmpty;
    pScreen->RegionExtents   = miRegionExtents;
    pScreen->RegionAppend    = miRegionAppend;
    pScreen->RegionValidate  = miRegionValidate;

    /* Bitmap/region conversion */
    pScreen->BitmapToRegion  = mfbPixmapToRegion; /* 1-bit pixmaps only */
    pScreen->RectsToRegion   = miRectsToRegion;

    /* Window painting - handled by ZZ9000draw.c */
    pScreen->PaintWindowBackground = ZZ9000PaintWindow;
    pScreen->PaintWindowBorder     = ZZ9000PaintWindow;
    pScreen->CopyWindow            = ZZ9000CopyWindow;

    /* Exposure and background clearing */
    pScreen->WindowExposures   = miWindowExposures;
    pScreen->ClearToBackground = miClearToBackground;

    /* Miscellaneous */
    pScreen->SendGraphicsExpose = miSendGraphicsExpose;

    /* X11R6 window-layer operations supplied by miScreenInit normally. */
    pScreen->MarkWindow             = miMarkWindow;
    pScreen->MarkOverlappedWindows  = miMarkOverlappedWindows;
    pScreen->ChangeSaveUnder        = miChangeSaveUnder;
    pScreen->PostChangeSaveUnder    = miPostChangeSaveUnder;
    pScreen->MoveWindow             = miMoveWindow;
    pScreen->ResizeWindow           = miSlideAndSizeWindow;
    pScreen->GetLayerWindow         = miGetLayerWindow;
    pScreen->HandleExposures        = miHandleValidateExposures;
    pScreen->ReparentWindow         = (void (*)()) 0;
#ifdef SHAPE
    pScreen->SetShape               = miSetShape;
#endif
    pScreen->ChangeBorderWidth      = miChangeBorderWidth;
    pScreen->MarkUnrealizedWindow   = miMarkUnrealizedWindow;

    /* Block/Wakeup and cursor are installed by rtgInit.c after this returns */

    ZZ9000Soft3DInit(pRTG);         /* zz9000draw.c: 3D clients' swaps */
    return TRUE;
}
