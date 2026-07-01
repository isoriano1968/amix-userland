/*
** ZZ9000win.c - Window lifecycle hooks for ZZ9000 RTG driver.
**
** The ZZ9000 is a plain framebuffer with no hardware window support.
** All ScreenRec window hooks are no-ops that return TRUE.
**
** PaintWindowBackground / PaintWindowBorder are in ZZ9000draw.c.
** CopyWindow is in ZZ9000draw.c.
*/

#include "X.h"
#include "windowstr.h"
#include "../../amix.h"
#include "../rtg.h"
#include "zz9000.h"

Bool
ZZ9000CreateWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}

Bool
ZZ9000DestroyWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}

Bool
ZZ9000PositionWindow(pWin, x, y)
WindowPtr pWin;
int       x, y;
{
    return TRUE;
}

Bool
ZZ9000ChangeWindowAttributes(pWin, mask)
WindowPtr     pWin;
unsigned long mask;
{
    return TRUE;
}

Bool
ZZ9000RealizeWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}

Bool
ZZ9000UnrealizeWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}
