/*
** ZZ9000cmap.c - Colormap management for ZZ9000 RTG (TrueColor, no CLUT).
**
** The ZZ9000 is a direct-colour framebuffer: pixel values are RGB565 words
** written directly to VRAM.  There is no hardware colour-lookup table, so:
**
**   InstallColormap   - bookkeeping only; WalkTree for TellLost/GainedMap.
**   UninstallColormap - reinstall the default colormap if needed.
**   ListInstalledColormaps - always returns 1 (exactly one map installed).
**   StoreColors       - no-op (no CLUT to update).
**   CreateColormap    - no-op (no CLUT entries to allocate).
**   DestroyColormap   - no-op.
**   ResolveColor      - round X11 16-bit RGB values to RGB565 precision.
**   CreateDefColormap - create default TrueColor map; AllocColor black/white.
*/

#include "X.h"
#include "Xproto.h"
#include "scrnintstr.h"
#include "colormapst.h"
#include "resource.h"
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

/* ------------------------------------------------------------------ */

Bool
ZZ9000CreateColormap(pMap)
ColormapPtr pMap;
{
    VisualPtr pVisual = pMap->pVisual;
    int       i, n;

    if (pVisual->class != TrueColor && pVisual->class != DirectColor)
        return TRUE;

    n = (pVisual->redMask   >> pVisual->offsetRed)   + 1;
    for (i = 0; i < n; i++)
        pMap->red[i].co.local.red     = (i * 65535) / (n - 1);

    n = (pVisual->greenMask >> pVisual->offsetGreen) + 1;
    for (i = 0; i < n; i++)
        pMap->green[i].co.local.green = (i * 65535) / (n - 1);

    n = (pVisual->blueMask  >> pVisual->offsetBlue)  + 1;
    for (i = 0; i < n; i++)
        pMap->blue[i].co.local.blue   = (i * 65535) / (n - 1);

    return TRUE;
}

void
ZZ9000DestroyColormap(pMap)
ColormapPtr pMap;
{
}

/* ------------------------------------------------------------------ */

/*
** ZZ9000InstallColormap - make pMap the current colormap.
**
** Notifies all clients via WalkTree(TellLostMap / TellGainedMap).
** No hardware CLUT writes are needed for TrueColor.
*/
void
ZZ9000InstallColormap(pMap)
ColormapPtr pMap;
{
    rtgScreenPtr pRTG = GetRTGScreen(pMap->pScreen);
    ColormapPtr  old  = pRTG->installedMap;

    if (pMap == old)
        return;

    if (old)
        WalkTree(pMap->pScreen, TellLostMap, (char *) &old->mid);

    pRTG->installedMap = pMap;
    WalkTree(pMap->pScreen, TellGainedMap, (char *) &pMap->mid);
}

/*
** ZZ9000UninstallColormap - uninstall pMap.
**
** If pMap is the currently installed map and it is not the default
** colormap, reinstall the default.
*/
void
ZZ9000UninstallColormap(pMap)
ColormapPtr pMap;
{
    rtgScreenPtr pRTG = GetRTGScreen(pMap->pScreen);

    if (pMap != pRTG->installedMap)
        return;

    if (pMap->mid != pMap->pScreen->defColormap)
    {
        ColormapPtr def = (ColormapPtr)
            LookupIDByType(pMap->pScreen->defColormap, RT_COLORMAP);
        (*pMap->pScreen->InstallColormap)(def);
    }
}

/*
** ZZ9000ListInstalledColormaps - return the ID of the installed map.
**
** X protocol guarantees exactly one colormap is installed at all times.
*/
int
ZZ9000ListInstalledColormaps(pScreen, pMaps)
ScreenPtr pScreen;
Colormap *pMaps;
{
    rtgScreenPtr pRTG = GetRTGScreen(pScreen);

    *pMaps = pRTG->installedMap->mid;
    return 1;
}

/*
** ZZ9000StoreColors - no-op for TrueColor.
**
** TrueColor pixels encode colour directly; there is no CLUT to update.
*/
void
ZZ9000StoreColors(pMap, ndef, pdefs)
ColormapPtr pMap;
int         ndef;
xColorItem *pdefs;
{
}

/* ------------------------------------------------------------------ */

/*
** ZZ9000ResolveColor - round X11 RGB values to RGB565 hardware precision.
**
** X stores colour components as 16-bit values (0-65535).  We quantise
** each component to the number of bits available in the hardware field
** (5 for red and blue, 6 for green) then expand back to 16 bits so that
** the DIX pixel value reflects exactly what the hardware will display.
**
** Two-step: find the hardware index (0..lim), then reconstruct the
** exact 16-bit value that index maps to in the colour table.  This
** avoids the unsigned-short overflow that plagued the earlier formula.
*/
void
ZZ9000ResolveColor(pRed, pGreen, pBlue, pVisual)
unsigned short *pRed, *pGreen, *pBlue;
VisualPtr       pVisual;
{
    unsigned int limr, limg, limb, idx;

    limr = pVisual->redMask   >> pVisual->offsetRed;
    limg = pVisual->greenMask >> pVisual->offsetGreen;
    limb = pVisual->blueMask  >> pVisual->offsetBlue;

    idx    = ((unsigned int)*pRed   * (limr + 1)) >> 16;
    *pRed  = (unsigned short)((idx * 65535) / limr);

    idx    = ((unsigned int)*pGreen * (limg + 1)) >> 16;
    *pGreen = (unsigned short)((idx * 65535) / limg);

    idx    = ((unsigned int)*pBlue  * (limb + 1)) >> 16;
    *pBlue  = (unsigned short)((idx * 65535) / limb);
}

/* ------------------------------------------------------------------ */

/*
** ZZ9000CreateDefColormap - create the default TrueColor colormap.
**
** Follows the cfbCreateDefColormap pattern:
**   1. Locate the visual that matches pScreen->rootVisual.
**   2. CreateColormap with AllocNone (TrueColor: client allocates cells).
**   3. AllocColor for white (all ones) and black (all zeros).
**   4. Install the new colormap.
**
** On success pScreen->whitePixel and pScreen->blackPixel are set by
** AllocColor to the nearest RGB565 representations of white and black.
*/
Bool
ZZ9000CreateDefColormap(pScreen)
ScreenPtr pScreen;
{
    unsigned short  zero = 0, ones = 0xFFFF;
    VisualPtr       pVisual;
    ColormapPtr     cmap;
    Pixel           whitePix, blackPix;

    for (pVisual = pScreen->visuals;
         pVisual->vid != pScreen->rootVisual;
         pVisual++)
        ;

    if (CreateColormap(pScreen->defColormap, pScreen, pVisual, &cmap,
                       AllocNone, 0) != Success)
        return FALSE;

    if (AllocColor(cmap, &ones, &ones, &ones, &whitePix, 0) != Success)
        return FALSE;

    if (AllocColor(cmap, &zero, &zero, &zero, &blackPix, 0) != Success)
        return FALSE;

    pScreen->whitePixel = whitePix;
    pScreen->blackPixel = blackPix;

    (*pScreen->InstallColormap)(cmap);
    return TRUE;
}
