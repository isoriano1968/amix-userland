#include    "amix.h"
#include    "resource.h"

struct scrtype DefaultScrType =
{
    SF_LESSRES|SF_LESSPLANES,
    ST_AMIGA,
    102400,
    102400,
    1,
    0,
};
int headsWanted = 1;			/* Number of screen heads wanted */
int spreadHeads = FALSE;		/* zaphod heads spread across scr groups? */
int group = -1;				/* default group number */
int XmonitorResolution = 90;
int YmonitorResolution = 90;
int amixCurrentScreenIndex = -1;

void amixMonoEnterLeave();

/*-
 *-----------------------------------------------------------------------
 * amixMonoInit --
 *	Attempt to find and initialize a monochrome framebuffer
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Most of the elements of the ScreenRec are filled in.  The
 *	video is enabled for the frame buffer...
 *
 *-----------------------------------------------------------------------
 */
/*ARGSUSED*/
static Bool
amixMonoInit (index, pScreen, argc, argv)
    int	    	  index;    	/* The index of pScreen in the ScreenInfo */
    ScreenPtr	  pScreen;  	/* The Screen to initialize */
    int	    	  argc;	    	/* The number of the Server's arguments. */
    char    	  **argv;   	/* The arguments themselves. Don't change! */
{
    if (!mfbScreenInit(pScreen,
		       (pointer)amixFbs[index].bp.bpl[0],
		       amixFbs[index].bp.width,
		       amixFbs[index].bp.height,
		       XmonitorResolution, YmonitorResolution,
		       amixFbs[index].bp.width))
	return (FALSE);

    if (!amixScreenAllocate (pScreen) || !amixScreenInit (pScreen))
	return FALSE;

    pScreen->whitePixel = 1;
    pScreen->blackPixel = 0;

    (void) amixSaveScreen(pScreen, SCREEN_SAVER_OFF);

    return mfbCreateDefColormap(pScreen);
}

/*-
 *-----------------------------------------------------------------------
 * amixMonoProbe --
 *	Attempt to find and initialize a monochrome framebuffer
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Memory is allocated for the frame buffer and the buffer is mapped. 
 *
 *-----------------------------------------------------------------------
 */

/*ARGSUSED*/
Bool
amixMonoProbe(pScreenInfo, index, fbNum, argc, argv)
    ScreenInfo	  *pScreenInfo;	/* The screenInfo struct */
    int	    	  index;    	/* The index of pScreen in the ScreenInfo */
    int	    	  fbNum;    	/* Index into the amixFbData array */
    int	    	  argc;	    	/* The number of the Server's arguments. */
    char    	  **argv;   	/* The arguments themselves. Don't change! */
{
    int fd;
    static struct Bitmap bp;
    static struct scolor scolor[] =
    {
	{ 0x00, 0x00, 0x00, 0x00, },
	{ 0xff, 0xff, 0xff, 0xff, },
    };
    char screenname[1024];
    extern char *display;

    if (amixFbs[index].scrtype.dispz == 0)
	amixFbs[index].scrtype = DefaultScrType;

    if (sprintf(screenname, "X11R5 Server :%s.%d", display, index) < 0)
	fd = OpenScreen("X11R5 Server", &amixFbs[index].scrtype, 0);
    else
	fd = OpenScreen(screenname, &amixFbs[index].scrtype, 0);

    if (fd < 0)
    {
	ErrorF("Couldn't open screen device (%s)\n", ScreenError());
	return FALSE;
    }

    if (fcntl(fd, F_SETFL, O_NDELAY) == -1)
    {
	ErrorF("Couldn't set O_NDELAY for screen device (%s)\n",
	       strerror(errno));

	goto CloseAndBarf;
    }

    if (ioctl(fd, SIOCGETTYPE, &amixFbs[index].scrtype))
    {
	ErrorF("Could get screen type (%s)\n", strerror(errno));

	goto CloseAndBarf;
    }

    if (ScreenColors(fd, scolor))
    {
	ErrorF("Couldn't set screen colors (%s)\n", ScreenError());

	goto CloseAndBarf;
    }

    memset(&bp, 0, (size_t) sizeof(bp));

    if (NewBitmap(fd, &bp) == -1)
    {
	ErrorF("NewBitmap(%d, ...) failed (%s)\n", fd, ScreenError());

	goto CloseAndBarf;
    }

    if (group == -1)
	group = ioctl(fd, SIOCGETGROUP, 0);
    else
	(void) ioctl(fd, SIOCSETGROUP, group);

    amixFbs[index].group = group;

    if (spreadHeads)
    {
	if (++group > 10)
	    group = 2;
    }

    if (DisplayScreen(fd))
    {
	ErrorF("DisplayScreen(%d) (%s)\n", fd, ScreenError());

	goto CloseBarfAndUnmap;
    }
	
    if (ioctl(fd, SIOCSETINPUTMODE, SIM_RAWKEY))
    {
	ErrorF("Coudln't set raw key input (%s)\n", strerror(errno));

    CloseBarfAndUnmap:
	munmap(bp.bpl[0], bp.height * bp.width / 8);

    CloseAndBarf:
	if (CloseScreen(fd))
	    ErrorF("CloseScreen(%d) failed (%s)\n", fd, ScreenError());

	return FALSE;
    }

    amixFbs[index].mapped = TRUE;
    amixFbs[index].fd = fd;
    amixFbs[index].bp = bp;
    amixFbs[index].EnterLeave = amixMonoEnterLeave;
#ifdef TIGA
    {
	extern KbPrivRec sysKbPriv;
	extern PtrPrivRec sysMousePriv;

	sysKbPriv.fd = sysMousePriv.fd = amixFbs[index].fd;
	if (amixCurrentScreenIndex == -1)
	    amixCurrentScreenIndex = index;
    }
#endif

    return TRUE;
}

void
amixMonoEnterLeave(pScreen, entering)
    ScreenPtr	pScreen;
    Bool	entering;
{
    extern KbPrivRec sysKbPriv;
    extern PtrPrivRec sysMousePriv;

    if (entering)
    {
	/*
	** Everything is handled as we enter a screen, thus
	** no need for an else clause
	*/
	sysKbPriv.fd = sysMousePriv.fd = amixFbs[pScreen->myNum].fd;

	(void) ioctl(amixFbs[pScreen->myNum].fd,
		     SIOCSETGROUP,
		     amixFbs[pScreen->myNum].group);
	(void) ioctl(amixFbs[pScreen->myNum].fd,
		     SIOCACTIVATEGROUP,
		     amixFbs[pScreen->myNum].group);
	DisplayScreen(amixFbs[pScreen->myNum].fd);
	amixCurrentScreenIndex = pScreen->myNum;
    }
}

Bool
amixZaphodHack(pScreen, x, y, index)
    ScreenPtr	*pScreen;
    int		*x, *y;
    int		index;
{
    static int new_index = -1;

    if (index == -1)
    {
	if (new_index != -1)
	{
	    *pScreen = screenInfo.screens[new_index];
	    *x = screenInfo.screens[new_index]->width / 2;
	    *y = screenInfo.screens[new_index]->height / 2;
	    new_index = -1;
	    return TRUE;
	}
	else
	    return FALSE;
    }
    else
    {
	if (new_index == index || index == amixCurrentScreenIndex)
	    return FALSE;
	else
	{
	    new_index = index;
	    return TRUE;
	}
    }
}

Bool
amixMonoCreate(pScreenInfo, argc, argv)
    ScreenInfo	  *pScreenInfo;
    int	    	  argc;
    char    	  **argv;
{
    return (AddScreen(amixMonoInit, argc, argv) >= 0);
}
