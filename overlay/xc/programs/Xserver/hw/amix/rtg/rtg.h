#ifndef AMIX_RTG_H
#define AMIX_RTG_H
/*
**  $Filename: ddx/amix/rtg/rtg.h $
**
**  xamix-rtg: Generic RTG (Retargetable Graphics) DDX layer for AMIX SVR4.
**
**  Common structures and declarations shared by all RTG card drivers.
**  Each card provides its own header (e.g. zz9000/zz9000.h) and source
**  files, and installs its drawing functions into the ScreenRec directly.
**
**  First supported card in this tree: MNT ZZ9000 (16-bit TrueColor RGB565).
*/

/*
** Per-screen private record allocated for each RTG screen.
** Stored in ScreenRec->devPrivates[rtgScreenIndex].
**
** Card-specific state lives in cardPrivate, pointed to by this struct.
*/
struct _rtgScreenRec {
    unsigned char      *frameBase;      /* mmap base returned by open/mmap   */
    unsigned short     *fbBase;         /* pixel (0,0) - frameBase+fbOffset  */
    int                 fbOffset;       /* byte offset from frameBase to fb   */
    int                 width;          /* screen width  in pixels            */
    int                 height;         /* screen height in pixels            */
    int                 pitch;          /* bytes per scanline                 */
    int                 bitsPerPixel;
    int                 depth;

    struct _ColormapRec *installedMap;  /* NULL for TrueColor (no CLUT)       */
    int               (*CloseScreen)(); /* chained CloseScreen                */

    unsigned char      *cardPrivate;    /* card-specific private data         */
    int                 devFd;          /* the card's device fd, -1 = none;   */
                                        /* set by the card's InitHW (RT-04)   */
};

/*
** The RTG monitor switch (P-8 / RT-04): the device-independent layer asks
** the card's own device to put the monitor on the RTG output at screen init
** and back on the Amiga video when the server ends, with the ioctl of the
** Xsvga-class RTG contract (Klaus Burckert; the numbering of Antti's
** va2000.h).  The card driver decides how: a line switch through the
** kernel's monsw, a passthrough register, ...  A kernel or card without it
** answers with an error, which is ignored.
*/
#ifndef SVGAIOCSetMonitorSwitch
#define SVGAIOC                 (0xe300)
#define SVGAIOCGetMonitorSwitch (SVGAIOC|0x06)
#define SVGAIOCSetMonitorSwitch (SVGAIOC|0x08)  /* note: 0x08 not 0x07 */
#define SVGAMONITORSWITCH_Amiga 1
#define SVGAMONITORSWITCH_SVGA  2
#endif

extern void rtgAbort();

typedef struct _rtgScreenRec rtgScreenRec;
typedef struct _rtgScreenRec *rtgScreenPtr;

extern int rtgScreenIndex;      /* allocated by AllocateScreenPrivateIndex() */
extern int rtgGCPrivateIndex;   /* allocated by AllocateGCPrivateIndex()     */

#define GetRTGScreen(s) \
    ((struct _rtgScreenRec *)(s)->devPrivates[rtgScreenIndex].ptr)

/*
** GC private: clip region derived from GC clip list.
** Same pattern as TIGA's GCPRIV.
*/
struct _rtg_gc_priv {
    struct _Region *clip;
};

typedef struct _rtg_gc_priv RTGGCPRIV;

#define rtgGCClip(gc) \
    (((struct _rtg_gc_priv *)(gc)->devPrivates[rtgGCPrivateIndex].ptr)->clip)

/*
** Entry points called from amixInit.c via amixFbData[].
*/
#ifdef RTG
extern int rtgProbe();
extern int rtgCreate();
#endif

#endif /* AMIX_RTG_H */
