/*
** ZZ9000pix.c - Pixmap and font operations for ZZ9000 RTG.
**
** Off-screen pixmaps live entirely in host memory (not in VRAM).
** Native 16-bit pixmaps use the X11R6 AllocatePixmap() layout.  Depth-1
** bitmap pixmaps use the VA2000/XRTG layout (PixmapRec followed directly
** by pixels) because these are the glyph/cursor masks consumed by mfb.
**
** Supported depths: 16 (native TrueColor) and 1 (bitmaps for fonts).
**
** RealizeFont / UnrealizeFont are no-ops; the ZZ9000 has no hardware
** font cache and font rendering is handled entirely by the mi layer.
*/

#include "X.h"
#include "scrnintstr.h"
#include "pixmap.h"
#include "pixmapstr.h"
#include "servermd.h"
#include "dixfont.h"
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

/*
** ZZ9000CreatePixmap - allocate a host-memory pixmap.
**
** PixmapRec, pixmap-private records, and pixel data are allocated in one
** block.  With PIXPRIV, pixel data starts at pScreen->totalPixmapSize;
** otherwise it starts immediately after PixmapRec.  devKind is the
** scanline size in bytes, padded to BITMAP_SCANLINE_PAD bits.
**
** Returns NullPixmap on unsupported depth or allocation failure.
*/
PixmapPtr
ZZ9000CreatePixmap(pScreen, width, height, depth)
ScreenPtr pScreen;
int       width;
int       height;
int       depth;
{
    PixmapPtr pPixmap;
    int       size;
    int       datasize;

    if (depth != ZZ9000_DEPTH && depth != 1)
        return NullPixmap;

    size     = PixmapBytePad(width, depth);
    datasize = height * size;

    if (depth == 1)
        pPixmap = (PixmapPtr) xalloc(sizeof(PixmapRec) + datasize);
    else
        pPixmap = AllocatePixmap(pScreen, datasize);

    if (!pPixmap)
        return NullPixmap;

    pPixmap->drawable.type         = DRAWABLE_PIXMAP;
    pPixmap->drawable.class        = 0;
    pPixmap->drawable.pScreen      = pScreen;
    pPixmap->drawable.depth        = depth;
    pPixmap->drawable.bitsPerPixel = depth;
    pPixmap->drawable.id           = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x            = 0;
    pPixmap->drawable.y            = 0;
    pPixmap->drawable.width        = width;
    pPixmap->drawable.height       = height;
    pPixmap->devKind               = size;
    pPixmap->refcnt                = 1;
    if (depth == 1)
        pPixmap->devPrivate.ptr = (pointer)(pPixmap + 1);
    else
    {
#ifdef PIXPRIV
        pPixmap->devPrivate.ptr = datasize ?
            (pointer)((char *)pPixmap + pScreen->totalPixmapSize) : NULL;
#else
        pPixmap->devPrivate.ptr = (pointer)(pPixmap + 1);
#endif
    }

    return pPixmap;
}

/*
** ZZ9000DestroyPixmap - release a host-memory pixmap.
**
** Decrements the reference count; frees only when it reaches zero.
** Because the header and data share one allocation, a single xfree
** releases both.
*/
Bool
ZZ9000DestroyPixmap(pPixmap)
PixmapPtr pPixmap;
{
    if (--pPixmap->refcnt)
        return TRUE;
    xfree((pointer) pPixmap);
    return TRUE;
}

/*
** ZZ9000RealizeFont - no hardware font cache; always succeeds.
*/
Bool
ZZ9000RealizeFont(pScreen, pFont)
ScreenPtr pScreen;
FontPtr   pFont;
{
    return TRUE;
}

/*
** ZZ9000UnrealizeFont - no hardware font cache; always succeeds.
*/
Bool
ZZ9000UnrealizeFont(pScreen, pFont)
ScreenPtr pScreen;
FontPtr   pFont;
{
    return TRUE;
}
