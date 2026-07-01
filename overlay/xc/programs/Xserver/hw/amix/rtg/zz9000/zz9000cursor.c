/*
** ZZ9000cursor.c - Software cursor for ZZ9000 RTG via mi layer.
**
** The ZZ9000 has no hardware sprite.  ZZ9000CursorInitialize() delegates
** to miDCInitialize(), which installs the miSprite/miDC software cursor
** machinery and overrides all ScreenRec cursor entries.
**
** The remaining functions are stubs installed as initial placeholders in
** the ScreenRec before miDCInitialize() is called.  In practice they are
** never reached; miDCInitialize() overwrites every ScreenRec slot.
*/

#include "X.h"
#include "Xmd.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "mipointer.h"
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

extern miPointerScreenFuncRec amixPointerScreenFuncs;
extern void miRecolorCursor();

/* ------------------------------------------------------------------ */
/* Stubs - overridden by miDCInitialize before any cursor is drawn     */

Bool
ZZ9000RealizeCursor(pScreen, pCursor)
ScreenPtr pScreen;
CursorPtr pCursor;
{
    return TRUE;
}

Bool
ZZ9000UnrealizeCursor(pScreen, pCursor)
ScreenPtr pScreen;
CursorPtr pCursor;
{
    return TRUE;
}

Bool
ZZ9000DisplayCursor(pScreen, pCursor)
ScreenPtr pScreen;
CursorPtr pCursor;
{
    return TRUE;
}

Bool
ZZ9000SetCursorPosition(pScreen, newx, newy, genEvent)
ScreenPtr    pScreen;
unsigned int newx, newy;
Bool         genEvent;
{
    return TRUE;
}

void
ZZ9000CursorLimits(pScreen, pCursor, pHotBox, pTopLeftBox)
ScreenPtr pScreen;
CursorPtr pCursor;
BoxPtr    pHotBox;
BoxPtr    pTopLeftBox;
{
    pTopLeftBox->x1 = max(pHotBox->x1, 0);
    pTopLeftBox->y1 = max(pHotBox->y1, 0);
    pTopLeftBox->x2 = min(pHotBox->x2, (int) pScreen->width);
    pTopLeftBox->y2 = min(pHotBox->y2, (int) pScreen->height);
}

void
ZZ9000PointerNonInterestBox(pScreen, pBox)
ScreenPtr pScreen;
BoxPtr    pBox;
{
}

void
ZZ9000ConstrainCursor(pScreen, pBox)
ScreenPtr pScreen;
BoxPtr    pBox;
{
}

/* ------------------------------------------------------------------ */

/*
** ZZ9000CursorInitialize - install the mi software cursor.
**
** Calls miDCInitialize() to wire the miSprite/miDC sprite layer into the
** ScreenRec.  After this returns, RealizeCursor ... ConstrainCursor all
** point to mi functions and the stubs above are unreachable.
**
** Also installs the remaining cursor entries so the ScreenRec is fully
** populated before miDCInitialize() performs its own override.
*/
void
ZZ9000CursorInitialize(pScreen)
ScreenPtr pScreen;
{
    pScreen->RealizeCursor         = ZZ9000RealizeCursor;
    pScreen->UnrealizeCursor       = ZZ9000UnrealizeCursor;
    pScreen->DisplayCursor         = ZZ9000DisplayCursor;
    pScreen->SetCursorPosition     = ZZ9000SetCursorPosition;
    pScreen->CursorLimits          = ZZ9000CursorLimits;
    pScreen->PointerNonInterestBox = ZZ9000PointerNonInterestBox;
    pScreen->ConstrainCursor       = ZZ9000ConstrainCursor;
    pScreen->RecolorCursor         = miRecolorCursor;

    miDCInitialize(pScreen, &amixPointerScreenFuncs);
}
