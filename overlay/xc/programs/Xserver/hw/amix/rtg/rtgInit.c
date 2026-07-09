/*
** rtgInit.c - RTG DDX entry points for xamix-rtg.
**
** Provides rtgProbe() and rtgCreate() which plug into amixFbData[] in
** amixInit.c, plus the static AddScreen callback rtgScreenInit().
**
** The amixFbData[] entry for RTG:
**   #ifdef RTG
**       rtgProbe, rtgCreate,
**   #endif
**
** rtgProbe() checks that a supported RTG card is present and fills in
** amixFbs[].  Currently only ZZ9000 is supported; ZZ9000Probe() does
** the actual hardware detection.
**
** rtgCreate() registers pixmap formats for 16-bit TrueColor and queues
** the screen for AddScreen().
**
** rtgScreenInit() is the AddScreen callback.  It allocates the
** rtgScreenRec, calls ZZ9000InitHW() to open the device and write the
** mode register sequence, then calls ZZ9000ScreenInit() to install the
** drawing function vector into the ScreenRec.
*/

#include "../amix.h"
#include "resource.h"
#include "pixmapstr.h"
#include "servermd.h"
#include <scrnintstr.h>
#include "mipointer.h"
#include "mi.h"
#include "rtg.h"
#include "zz9000/zz9000.h"

extern int  XmonitorResolution;
extern int  YmonitorResolution;
extern unsigned long amixGeneration;
extern fbFd amixFbs[];
extern miPointerScreenFuncRec amixPointerScreenFuncs;
extern void amixBlockHandler();
extern void amixWakeupHandler();
extern Bool amixCursorInitialize();

/* forward - defined in ZZ9000/ZZ9000screen.c */
extern Bool ZZ9000ScreenInit();
extern Bool ZZ9000CreateDefColormap();

/* mfb - needed to initialise depth-1 GC private storage */
extern Bool mfbAllocatePrivates();

/* input device globals from amixKbd.c / amixMouse.c / amixMono.c */
extern KbPrivRec  sysKbPriv;
extern PtrPrivRec sysMousePriv;
extern int        amixCurrentScreenIndex;
extern struct scrtype DefaultScrType;

/* ------------------------------------------------------------------ */
/* Globals defined here, declared extern in rtg.h                     */

int rtgScreenIndex    = -1;
int rtgGCPrivateIndex = -1;

/* ------------------------------------------------------------------ */
/* TrueColor 16-bit visual for RGB565                                  */

static VisualRec rtgVisual;     /* filled in rtgCreate() each generation */
static VisualID  rtgVID;
static DepthRec  rtgDepth;

static unsigned long rtgGeneration = 0;

/* ------------------------------------------------------------------ */

/*
** rtgWakeupHandler - fallback no-op wakeup handler.
**
** Used only when rtgProbe() failed to open the AMIX screen device
** (no input available).  amixWakeupHandler calls amixFindInputScreen
** which does FD_ISSET(amixFbs[nscreen].fd, pReadmask); with fd=-1
** that is undefined behaviour and crashes on m68k SVR4.
*/
static void
rtgWakeupHandler(nscreen, pbdata, err, pReadmask)
int           nscreen;
pointer       pbdata;
unsigned long err;
pointer       pReadmask;
{
    (void)nscreen; (void)pbdata; (void)err; (void)pReadmask;
}

static Bool
rtgSaveScreen(pScreen, on)
ScreenPtr pScreen;
int       on;
{
    if (on != SCREEN_SAVER_ON)
    {
        SetTimeSinceLastInputEvent();
        return TRUE;
    }
    return FALSE;
}

static Bool
rtgCloseScreen(index, pScreen)
int       index;
ScreenPtr pScreen;
{
    rtgScreenPtr pRTG = GetRTGScreen(pScreen);
    Bool         ret;
    int          i = pScreen->myNum;

    pScreen->CloseScreen = pRTG->CloseScreen;
    ret = (*pScreen->CloseScreen)(index, pScreen);
    (*pScreen->SaveScreen)(pScreen, SCREEN_SAVER_OFF);

    if (amixFbs[i].mapped && amixFbs[i].fd >= 0)
    {
        if (CloseScreen(amixFbs[i].fd))
            ErrorF("rtgCloseScreen: CloseScreen(%d) failed\n", amixFbs[i].fd);
        amixFbs[i].fd     = -1;
        amixFbs[i].mapped = FALSE;
    }

    ZZ9000CloseHW(pRTG);
    xfree((pointer) pRTG);

    return ret;
}

/* ------------------------------------------------------------------ */

/*
** rtgOpenInput - open the AMIX screen device for keyboard/mouse input.
**
** Extracted from rtgProbe() so it can also be re-run from rtgScreenInit().
** amixInit.c's InitOutput() guards the probeProc loop (rtgProbe among
** them) with the static, process-lifetime flag amixDevsProbed, so
** rtgProbe() only ever runs on the very first server generation - not on
** every server reset.  But rtgCloseScreen() above unconditionally closes
** amixFbs[i].fd and clears amixFbs[i].mapped every time a generation's
** screen is torn down.  Without re-opening it here, amixFbs[index].mapped
** stays FALSE for the rest of the process after the first reset, which
** makes rtgScreenInit() install the no-op rtgWakeupHandler instead of
** amixWakeupHandler - i.e. mouse/keyboard input silently stops being
** read at all, and the cursor appears permanently stuck.
**
** Same OpenScreen/SIOCACTIVATE/SIOCSETINPUTMODE sequence as before;
** just callable from more than one place now.
*/
static void
rtgOpenInput(index)
int index;
{
    extern char *display;
    char screenname[1024];
    int  fd;

    if (amixFbs[index].scrtype.dispz == 0)
        amixFbs[index].scrtype = DefaultScrType;

    if (sprintf(screenname, "Xrtg :%s.%d", display, index) < 0)
        fd = OpenScreen("Xrtg", &amixFbs[index].scrtype, 0);
    else
        fd = OpenScreen(screenname, &amixFbs[index].scrtype, 0);

    if (fd < 0)
    {
        ErrorF("rtgOpenInput: OpenScreen failed (%s); starting display-only\n",
               ScreenError());
        amixFbs[index].fd     = -1;
        amixFbs[index].mapped = FALSE;
        return;
    }

    if (fcntl(fd, F_SETFL, O_NDELAY) == -1)
        ErrorF("rtgOpenInput: F_SETFL O_NDELAY failed (%s)\n",
               strerror(errno));

    /* SIOCACTIVATE (SelectScreen) makes our screen context the active
    ** one for keyboard/mouse event delivery without calling DisplayScreen.
    ** DisplayScreen would require NewBitmap (chip RAM for a native bitmap)
    ** and would take over the ECS display, causing the screen manager to
    ** send SIGHUP to the X server when the native console loses its screen.
    ** SIOCACTIVATE selects our screen as the event target without switching
    ** the displayed screen group, so the native Amiga display is unaffected
    ** and no spurious SIGHUP is generated. */
    if (ioctl(fd, SIOCACTIVATE, 0))
        ErrorF("rtgOpenInput: SIOCACTIVATE failed (%s)\n", strerror(errno));

    if (ioctl(fd, SIOCSETINPUTMODE, SIM_RAWKEY))
        ErrorF("rtgOpenInput: SIOCSETINPUTMODE SIM_RAWKEY failed (%s)\n",
               strerror(errno));

    amixFbs[index].fd     = fd;
    amixFbs[index].mapped = TRUE;
    amixFbs[index].group  = ioctl(fd, SIOCGETGROUP, 0);

    sysKbPriv.fd           = fd;
    sysMousePriv.fd        = fd;
    amixCurrentScreenIndex = index;

    ErrorF("rtgOpenInput: input fd=%d group=%d\n", fd, amixFbs[index].group);
}

/*
** rtgScreenInit - AddScreen callback, called once per screen by DIX.
**
** Allocates the rtgScreenRec, initialises hardware via ZZ9000InitHW(),
** installs the drawing function vector via ZZ9000ScreenInit(), then
** wires up CloseScreen, SaveScreen, block handlers and cursor.
*/
static Bool
rtgScreenInit(index, pScreen, argc, argv)
int       index;
ScreenPtr pScreen;
int       argc;
char    **argv;
{
    rtgScreenPtr pRTG;

    ErrorF("rtgScreenInit: index=%d\n", index);

    pRTG = (rtgScreenPtr) xalloc(sizeof(rtgScreenRec));
    if (!pRTG)
    {
        ErrorF("rtgScreenInit: xalloc pRTG failed\n");
        return FALSE;
    }
    memset((char *) pRTG, 0, sizeof(rtgScreenRec));

    pScreen->devPrivates[rtgScreenIndex].ptr = (pointer) pRTG;

    /* Screen geometry from selected mode */
    pRTG->width        = ZZ9000_selected_mode->w;
    pRTG->height       = ZZ9000_selected_mode->h;
    pRTG->pitch        = ZZ9000_selected_mode->w * 2;
    pRTG->bitsPerPixel = ZZ9000_BPP;
    pRTG->depth        = ZZ9000_DEPTH;
    pRTG->fbOffset     = ZZ9000_FB_OFFSET;

    ErrorF("rtgScreenInit: geometry set, rtgGCPrivateIndex=%d\n", rtgGCPrivateIndex);

    /* mfb GC private storage - required so depth-1 GCs can be delegated to
       mfbCreateGC (same pattern as cfb/cfbscrinit.c:mfbAllocatePrivates) */
    if (!mfbAllocatePrivates(pScreen, (int *)NULL, (int *)NULL))
    {
        ErrorF("rtgScreenInit: mfbAllocatePrivates failed\n");
        xfree((pointer) pRTG);
        return FALSE;
    }

    /* GC private storage for this screen */
    if (!AllocateGCPrivate(pScreen, rtgGCPrivateIndex, sizeof(RTGGCPRIV)))
    {
        ErrorF("rtgScreenInit: AllocateGCPrivate failed\n");
        xfree((pointer) pRTG);
        return FALSE;
    }

    ErrorF("rtgScreenInit: AllocateGCPrivate ok, calling ZZ9000InitHW\n");

    /* Open /dev/zz9000, set mode, mmap framebuffer */
    if (!ZZ9000InitHW(pRTG))
    {
        ErrorF("rtgScreenInit: ZZ9000InitHW failed\n");
        xfree((pointer) pRTG);
        return FALSE;
    }

    ErrorF("rtgScreenInit: hw init done, calling ZZ9000ScreenInit\n");

    /*
    ** Install the ScreenRec function vector for ZZ9000.
    ** ZZ9000ScreenInit() also sets pScreen->numVisuals, visuals, depths,
    ** rootDepth, rootVisual, defColormap, blackPixel, whitePixel.
    */
    if (!ZZ9000ScreenInit(pScreen, pRTG,
                          XmonitorResolution, YmonitorResolution,
                          &rtgVisual, &rtgDepth))
    {
        ErrorF("rtgScreenInit: ZZ9000ScreenInit failed\n");
        ZZ9000CloseHW(pRTG);
        xfree((pointer) pRTG);
        return FALSE;
    }

    ErrorF("rtgScreenInit: screen init done, chaining CloseScreen\n");

    /* Chain CloseScreen and install RTG cleanup */
    pRTG->CloseScreen   = pScreen->CloseScreen;
    pScreen->CloseScreen = rtgCloseScreen;
    pScreen->SaveScreen  = rtgSaveScreen;

    /* rtgProbe() only opens the input device on the very first server
    ** generation (amixDevsProbed in amixInit.c gates the whole probeProc
    ** loop to run once per process); rtgCloseScreen() closes it on every
    ** reset.  Re-open it here whenever it isn't already mapped so input
    ** keeps working past the first server reset. */
    if (!amixFbs[index].mapped)
        rtgOpenInput(index);

    /* Autorepeat handlers; use real amixWakeupHandler when input fd is open */
    pScreen->BlockHandler  = amixBlockHandler;
    pScreen->WakeupHandler = amixFbs[index].mapped
                             ? amixWakeupHandler : rtgWakeupHandler;
    pScreen->blockData     = (pointer) NULL;
    pScreen->wakeupData    = (pointer) NULL;

    ErrorF("rtgScreenInit: calling amixCursorInitialize\n");

    /* Software cursor - ZZ9000 has no hardware sprite */
    if (!amixCursorInitialize(pScreen))
    {
        ErrorF("rtgScreenInit: amixCursorInitialize returned FALSE, using miDC\n");
        miDCInitialize(pScreen, &amixPointerScreenFuncs);
    }

    ErrorF("rtgScreenInit: cursor done, calling ZZ9000CreateDefColormap\n");

    (void) rtgSaveScreen(pScreen, SCREEN_SAVER_FORCER);

    if (!ZZ9000CreateDefColormap(pScreen))
    {
        ErrorF("rtgScreenInit: ZZ9000CreateDefColormap failed\n");
        return FALSE;
    }

    ErrorF("rtgScreenInit: done, returning TRUE\n");
    return TRUE;
}

/* ------------------------------------------------------------------ */

/*
** rtgProbe - probeProc entry in amixFbData[].
**
** Called by InitOutput() to verify an RTG card is accessible.
** Fills in the amixFbs[index] entry with geometry and opens the
** AMIX screen device for keyboard/mouse input.
**
** Input strategy - same as amixMonoProbe but without NewBitmap:
**   OpenScreen()     - allocate an AMIX screen context; returns event fd
**   DisplayScreen()  - make our context active; AMIX routes input here
**   SIOCSETINPUTMODE - request raw keycodes from the keyboard
**
** DisplayScreen does not affect ZZ9000 VRAM output (the card reads its
** own framebuffer directly).  It only tells the AMIX event system which
** screen context is "front" so keyboard and mouse events are delivered
** to our fd.  This is confirmed by Klaus Burkert's Xsvga which follows
** the same OpenScreen/DisplayScreen path for input while driving the
** SVGA card through a separate /dev/svga* device.
**
** If OpenScreen fails the server starts without input (display-only).
** amixFbs[index].mapped = FALSE keeps amixFindInputScreen safe.
*/
Bool
rtgProbe(pScreenInfo, index, fbNum, argc, argv)
ScreenInfo *pScreenInfo;
int         index;
int         fbNum;
int         argc;
char      **argv;
{
    ErrorF("rtgProbe: index=%d\n", index);

    if (!ZZ9000Probe())
    {
        ErrorF("rtgProbe: RTG card not found at %s\n", ZZ9000_DEV);
        return FALSE;
    }

    /* Open the AMIX screen device for input events.  rtgScreenInit() also
    ** calls this on later server generations, since this probeProc itself
    ** only runs once per process (see rtgOpenInput's comment above). */
    rtgOpenInput(index);

    amixFbs[index].bp.width  = ZZ9000_selected_mode->w;
    amixFbs[index].bp.height = ZZ9000_selected_mode->h;

    ErrorF("rtgProbe: ok, width=%d height=%d\n", ZZ9000_selected_mode->w, ZZ9000_selected_mode->h);
    return TRUE;
}

/*
** rtgCreate - createProc entry in amixFbData[].
**
** Called by InitOutput() after a successful rtgProbe().  Allocates the
** per-generation screen and GC private indices, initialises the
** TrueColor VisualRec for RGB565, sets the 16-bit pixmap format, and
** registers the screen with the DIX layer via AddScreen().
*/
Bool
rtgCreate(pScreenInfo, argc, argv)
ScreenInfo *pScreenInfo;
int         argc;
char      **argv;
{
    ErrorF("rtgCreate: called, serverGeneration=%d\n", serverGeneration);

    if (rtgGeneration != serverGeneration)
    {
        rtgScreenIndex = AllocateScreenPrivateIndex();
        ErrorF("rtgCreate: rtgScreenIndex=%d\n", rtgScreenIndex);
        if (rtgScreenIndex < 0)
        {
            ErrorF("rtgCreate: AllocateScreenPrivateIndex failed\n");
            return FALSE;
        }

        rtgGCPrivateIndex = AllocateGCPrivateIndex();
        ErrorF("rtgCreate: rtgGCPrivateIndex=%d\n", rtgGCPrivateIndex);
        if (rtgGCPrivateIndex < 0)
        {
            ErrorF("rtgCreate: AllocateGCPrivateIndex failed\n");
            return FALSE;
        }

        /* TrueColor visual for 16-bit RGB565 */
        rtgVisual.vid            = FakeClientID(0);
        rtgVisual.class          = TrueColor;
        rtgVisual.bitsPerRGBValue = 8;
        rtgVisual.ColormapEntries = 64;   /* 2^6 - green channel dominates */
        rtgVisual.nplanes        = ZZ9000_DEPTH;
        rtgVisual.redMask        = ZZ9000_RED_MASK;
        rtgVisual.greenMask      = ZZ9000_GREEN_MASK;
        rtgVisual.blueMask       = ZZ9000_BLUE_MASK;
        rtgVisual.offsetRed      = ZZ9000_RED_SHIFT;
        rtgVisual.offsetGreen    = ZZ9000_GREEN_SHIFT;
        rtgVisual.offsetBlue     = ZZ9000_BLUE_SHIFT;

        rtgVID             = rtgVisual.vid;
        rtgDepth.depth     = ZZ9000_DEPTH;
        rtgDepth.numVids   = 1;
        rtgDepth.vids      = &rtgVID;

        rtgGeneration = serverGeneration;
    }

    pScreenInfo->numPixmapFormats = 2;

    pScreenInfo->formats[0].depth        = ZZ9000_DEPTH;
    pScreenInfo->formats[0].bitsPerPixel = ZZ9000_BPP;
    pScreenInfo->formats[0].scanlinePad  = BITMAP_SCANLINE_PAD;

    pScreenInfo->formats[1].depth        = 1;
    pScreenInfo->formats[1].bitsPerPixel = 1;
    pScreenInfo->formats[1].scanlinePad  = BITMAP_SCANLINE_PAD;

    ErrorF("rtgCreate: calling AddScreen\n");
    return (AddScreen(rtgScreenInit, argc, argv) >= 0);
}
