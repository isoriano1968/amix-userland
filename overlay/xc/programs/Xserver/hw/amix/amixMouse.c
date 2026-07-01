/*-
 * amixMouse.c --
 *	Functions for playing cat and mouse... sorry.
 *
 * Copyright (c) 1987 by the Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 */

/************************************************************
Copyright 1987 by Sun Microsystems, Inc. Mountain View, CA.

                    All Rights Reserved

Permission  to  use,  copy,  modify,  and  distribute   this
software  and  its documentation for any purpose and without
fee is hereby granted, provided that the above copyright no-
tice  appear  in all copies and that both that copyright no-
tice and this permission notice appear in  supporting  docu-
mentation,  and  that the names of Sun or MIT not be used in
advertising or publicity pertaining to distribution  of  the
software  without specific prior written permission. Sun and
M.I.T. make no representations about the suitability of this
software for any purpose. It is provided "as is" without any
express or implied warranty.

SUN DISCLAIMS ALL WARRANTIES WITH REGARD TO  THIS  SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FIT-
NESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SUN BE  LI-
ABLE  FOR  ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,  DATA  OR
PROFITS,  WHETHER  IN  AN  ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.

********************************************************/

#ifndef	lint
static char sccsid[] = "%W %G Copyright 1987 Sun Micro";
#endif

#define NEED_EVENTS
#include    "amix.h"
#include    "mipointer.h"

Bool ActiveZaphod = TRUE;
int amixFlushScreenInputIndex = -1;

static Bool amixCursorOffScreen();
static void amixCrossScreen();

miPointerScreenFuncRec amixPointerScreenFuncs = {
    amixCursorOffScreen,
    amixCrossScreen,
    miPointerWarpCursor,
};

static void 	  	amixMouseCtrl();
static struct InputEvent *amixMouseGetEvents();
void 		  	amixMouseEnqueueEvent();

PtrPrivRec 	sysMousePriv = {
    0,				/* Fd for current device */
    amixMouseGetEvents,		/* Function to read events */
    amixMouseEnqueueEvent,	/* Function to process an event */
};

/*-
 *-----------------------------------------------------------------------
 * amixMouseProc --
 *	Handle the initialization, etc. of a mouse
 *
 * Results:
 *	none.
 *
 * Side Effects:
 *
 * Note:
 *	When using amixwindows, all input comes off a single fd, stored in the
 *	global windowFd.  Therefore, only one device should be enabled and
 *	disabled, even though the application still sees both mouse and
 *	keyboard.  We have arbitrarily chosen to enable and disable windowFd
 *	in the keyboard routine amixKbdProc rather than in amixMouseProc.
 *
 *-----------------------------------------------------------------------
 */
int
amixMouseProc (pMouse, what)
    DevicePtr	  pMouse;   	/* Mouse to play with */
    int	    	  what;	    	/* What to do with it */
{
    register int  fd;
    int	    	  format;
    static int	  oformat;
    BYTE    	  map[4];
    char	  *device, *getenv ();

    switch (what) {
	case DEVICE_INIT:
	    if (pMouse != LookupPointerDevice()) {
		ErrorF ("Cannot open non-system mouse");	
		return (!Success);
	    }

	    pMouse->on = FALSE;
	    map[1] = 1;
	    map[2] = 2;
	    map[3] = 3;

	    InitPointerDeviceStruct(
		pMouse, map, 3, miPointerGetMotionEvents,
 		amixMouseCtrl, miPointerGetMotionBufferSize());
	    break;

	case DEVICE_ON:
	    pMouse->on = TRUE;
	    break;

	case DEVICE_CLOSE:
	    break;

	case DEVICE_OFF:
	    pMouse->on = FALSE;
	    break;
    }
    return (Success);
}
	    
/*-
 *-----------------------------------------------------------------------
 * amixMouseCtrl --
 *	Alter the control parameters for the mouse. Since acceleration
 *	etc. is done from the PtrCtrl record in the mouse's device record,
 *	there's nothing to do here.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
/*ARGSUSED*/
static void
amixMouseCtrl (pMouse)
    DevicePtr	  pMouse;
{
}

/*-
 *-----------------------------------------------------------------------
 * amixMouseGetEvents --
 *	Return the events waiting in the wings for the given mouse.
 *
 * Results:
 *	A pointer to an array of Firm_events or (Firm_event *)0 if no events
 *	The number of events contained in the array.
 *	A boolean as to whether more events might be available.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------------
 */
static struct InputEvent *
amixMouseGetEvents (pMouse, pNumEvents, pAgain)
    DevicePtr	  pMouse;	    /* Mouse to read */
    int	    	  *pNumEvents;	    /* Place to return number of events */
    Bool	  *pAgain;	    /* whether more might be available */
{
    int	    	  nBytes;	    /* number of bytes of events available. */
    static struct InputEvent evBuf[MAXEVENTS];   /* Buffer for Firm_events */
    PtrPrivPtr pPriv = (PtrPrivPtr) pMouse->devicePrivate;

    nBytes = read (pPriv->fd, evBuf, sizeof(evBuf));

    if (nBytes < 0) {
	if (errno == EWOULDBLOCK) {
	    *pNumEvents = 0;
	    *pAgain = FALSE;
	} else {
	    Error ("Reading mouse");
	    FatalError ("Could not read from mouse");
	}
    } else {
	*pNumEvents = nBytes / sizeof (struct InputEvent);
	*pAgain = (nBytes == sizeof (evBuf));
    }
    return (evBuf);
}


/*-
 *-----------------------------------------------------------------------
 * MouseAccelerate --
 *	Given a delta and a mouse, return the acceleration of the delta.
 *
 * Results:
 *	The corrected delta
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
static short
MouseAccelerate (pMouse, delta)
    DevicePtr	  pMouse;
    int	    	  delta;
{
    register int  sgn = sign(delta);
    register PtrCtrl *pCtrl;

    delta = abs(delta);
    pCtrl = &((DeviceIntPtr) pMouse)->ptrfeed->ctrl;

    if (delta > pCtrl->threshold) {
	return ((short) (sgn * (pCtrl->threshold +
				((delta - pCtrl->threshold) * pCtrl->num) /
				pCtrl->den)));
    } else {
	return ((short) (sgn * delta));
    }
}

/*-
 *-----------------------------------------------------------------------
 * amixMouseEnqueueEvent --
 *	Given a Firm_event for a mouse, pass it off the the dix layer
 *	properly converted...
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The cursor may be redrawn...? devPrivate/x/y will be altered.
 *
 *-----------------------------------------------------------------------
 */
void
amixMouseEnqueueEvent (pMouse, fe)
    DevicePtr	  pMouse;   	/* Mouse from which the event came */
    struct InputEvent *fe;    	/* Event to process */
{
    xEvent		xE;
    register int  	bmask;	/* Temporary button mask */
    register unsigned long  time;
    int			x, y;
    extern int amixCurrentScreenIndex;

    time = xE.u.keyButtonPointer.time = TVTOMILLI(fe->tv);

    switch (fe->type)
    {
    case MOUSE_BUTTON_EVENT:		/* A button changed state */
	/*
	** Raw keyboard code for mouse buttons:
	** (fe->code & 0x7F) - 0x7B;	    (1= left; 2=middle; 3=right)
	**
	** Up or Down are designated by:
	** (fe->code & 0x80)		    (Z=down NZ=up)
	*/

	xE.u.u.detail = (fe->code & 0x7F) - 0x7B; /* Which button */

	if (fe->code & 0x80)	/* Releasedp? */
	    xE.u.u.type = ButtonRelease;
	else 	
	    xE.u.u.type = ButtonPress;
    
	mieqEnqueue (&xE);
	break;

    case MOUSE_MOVE_EVENT:
	/*
	** fe->code, the high order eight bits are the signed delta x.
	** the low order eight bits are the signed delta y
	*/

	switch (amixFbs[amixCurrentScreenIndex].type)
	{
	default:
	case NATIVE_MONO:
	    miPointerDeltaCursor(MouseAccelerate(pMouse, (signed char)
						 ((fe->code >> 8) & 0xFF)),
				 MouseAccelerate(pMouse, (signed char)
						 (fe->code & 0xFF)),
				 time);
	    break;

#ifdef TIGA
	case DMI_RESOLVER:
	    tigDeltaCursor(screenInfo.screens[amixCurrentScreenIndex],
			   MouseAccelerate(pMouse,
					   (signed char)((fe->code >> 8) & 0xFF)),
			   MouseAccelerate(pMouse, (signed char)
					   (fe->code & 0xFF)),
			   time);
	    break;
#endif /* TIGA */
	}
	break;
    }
}

/*ARGSUSED*/
static Bool
amixCursorOffScreen (pScreen, x, y)
    ScreenPtr	*pScreen;
    int		*x, *y;
{
    int	    index;

    if (amixZaphodHack(pScreen, x, y, -1))
	return TRUE;
    /*
     * Active Zaphod implementation:
     *    increment or decrement the current screen
     *    if the x is to the right or the left of
     *    the current screen.
     */
    if (ActiveZaphod &&
	screenInfo.numScreens > 1 && (*x >= (*pScreen)->width || *x < 0))
    {
	index = (*pScreen)->myNum;
	amixFlushScreenInputIndex = index;
	if (*x < 0)
	{
	    index = (index ? index : screenInfo.numScreens) - 1;
	    *pScreen = screenInfo.screens[index];
	    *x += (*pScreen)->width;
	}
	else
	{
	    *x -= (*pScreen)->width;
	    index = (index + 1) % screenInfo.numScreens;
	    *pScreen = screenInfo.screens[index];
	}
	return TRUE;
    }
    return FALSE;
}

static void
amixCrossScreen (pScreen, entering)
    ScreenPtr	pScreen;
    Bool	entering;
{
    if (amixFbs[pScreen->myNum].EnterLeave)
	(*amixFbs[pScreen->myNum].EnterLeave) (pScreen, entering);
}
