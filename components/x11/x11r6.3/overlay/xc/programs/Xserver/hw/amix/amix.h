/*-
 * amix.h --
 *	Internal declarations for the amix ddx interface
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
 */
#ifndef _AMIX_H_
#define _AMIX_H_

#include    <errno.h>
extern int  errno;
#include    <sys/param.h>
#include    <sys/types.h>
#include    <sys/time.h>
#include    <sys/file.h>
#include    <sys/fcntl.h>
#include    <sys/signal.h>
#include    "X.h"
#include    "Xproto.h"
#include    "misc.h"
#include    "scrnintstr.h"
#ifdef NEED_EVENTS
#include    "inputstr.h"
#endif /* NEED_EVENTS */
#include    "input.h"
#include    "colormapst.h"
#include    "dix.h"
#include    "cursor.h"
#include <amiga/screen.h>
#include "lscreen.h"

struct InputEvent
{
    unsigned char type;
    unsigned char class;
    unsigned short code;
    unsigned long qualifiers;
    struct timeval tv;
};

/*
 * MAXEVENTS is the maximum number of events the mouse and keyboard functions
 * will read on a given call to their GetEvents vectors.
 */
#define MAXEVENTS 	32

/*
 * MAXSCREENS is the maximum number of heads Zaphod is allowed to
 * have.  These heads are currently restricted to native mode Amiga
 * Screens
 */
#define MAX_SCREENS	10

/*
 * Data private to any amix keyboard.
 *	GetEvents reads any events which are available for the keyboard
 *	ProcessEvent processes a single event and gives it to DIX
 *	DoneEvents is called when done handling a string of keyboard
 *	    events or done handling all events.
 *	devPrivate is private to the specific keyboard.
 *	map_q is TRUE if the event queue for the keyboard is memory mapped.
 */
typedef struct kbPrivate {
    int fd;				/* file descriptor for device */
    struct InputEvent *(*GetEvents)(); 	/* Function to read events */
    void    	  (*EnqueueEvent)();	/* Function to process an event */
    KeybdCtrl	  *ctrl;    	    	/* Current control structure (for
 					 * keyclick, bell duration, auto-
 					 * repeat, etc.) */
} KbPrivRec, *KbPrivPtr;

/*
 * Data private to any amix pointer device.
 *	GetEvents, ProcessEvent and DoneEvents have uses similar to the
 *	    keyboard fields of the same name.
 *	pScreen is the screen the pointer is on (only valid if it is the
 *	    main pointer device).
 *	dx and dy are relative coordinates on that screen (they may be negative)
 */
typedef struct ptrPrivate {
    int fd;				/* file descriptor for device */
    struct InputEvent *(*GetEvents)();  	/* Function to read events */
    void    	  (*EnqueueEvent)();	/* Function to process an event */
} PtrPrivRec, *PtrPrivPtr;

extern int  amixScreenIndex;

typedef struct
{
    ColormapPtr	    installedMap;
    Bool	    (*CloseScreen)();
    void	    (*UpdateColormap)();
} amixScreenRec, *amixScreenPtr;

#define GetScreenPrivate(s)   ((amixScreenPtr) ((s)->devPrivates[amixScreenIndex].ptr))
#define SetupScreen(s)	amixScreenPtr	pPrivate = GetScreenPrivate(s)

/*
 * Frame-buffer-private info.
 *	fd  	  	file opened to the frame buffer device.
 *	info	  	description of the frame buffer -- type, height, depth,
 *	    	  	width, etc.
 *	fb  	  	pointer to the mapped image of the frame buffer. Used
 *	    	  	by the driving routines for the specific frame buffer
 *	    	  	type.
 *	mapped	  	flag set true by the driver when the frame buffer has
 *	    	  	been mapped in.
 *	parent	  	set true if the frame buffer is actually a SunWindows
 *	    	  	window.
 *	fbPriv	  	Data private to the frame buffer type.
 */
typedef struct {
    int			type;		/* Display type */
    Bool    	  	mapped;		/* TRUE if frame buffer mapped */
    int	    	  	fd;		/* Descriptor open to frame buffer */
    int			group;		/* screen group we are member of */
    struct scrtype 	scrtype;	/* Frame buffer characteristics */
    struct Bitmap 	bp;		/* Frame buffer characteristics */
    void		(*EnterLeave)();    /* screen switch */
} fbFd;

extern Bool amixSupportsDepth8;
extern unsigned long amixGeneration;


typedef struct _amixFbDataRec {
    Bool    (*probeProc)();	/* probe procedure for this fb */
    Bool    (*createProc)();	/* create procedure for this fb */
} amixFbDataRec;

extern amixFbDataRec amixFbData[];
/*
 * Cursor functions
 */
extern void 	  amixInitCursor();

/*
 * Initialization
 */
extern Bool 	  amixScreenInit();

extern fbFd 	  amixFbs[];

extern int  	  lastEventTime;    /* Time (in ms.) of last event */
extern void 	  SetTimeSinceLastInputEvent();

#define AUTOREPEAT_INITIATE	(500)		/* milliseconds */
#define AUTOREPEAT_DELAY	(25)		/* milliseconds */
/*
 * We signal autorepeat events with the unique Firm_event
 * id AUTOREPEAT_EVENTID.
 * Because inputevent ie_code is set to Firm_event ids in
 * amixKbdProcessEventSunWin, and ie_code is short whereas
 * Firm_event id is u_short, we use 0x7fff.
 */
#define AUTOREPEAT_EVENTID      (0x7fff)        /* AutoRepeat Firm_event id */

extern int	autoRepeatKeyDown;		/* TRUE if key down */
extern int	autoRepeatReady;		/* TRUE if time out */
extern int	autoRepeatDebug;		/* TRUE if debugging */
extern long	autoRepeatInitiate;
extern long 	autoRepeatDelay;
extern struct timeval autoRepeatLastKeyDownTv;
extern struct timeval autoRepeatDeltaTv;

#define tvminus(tv, tv1, tv2)	/* tv = tv1 - tv2 */ \
		if ((tv1).tv_usec < (tv2).tv_usec) { \
			(tv1).tv_usec += 1000000; \
			(tv1).tv_sec -= 1; \
		} \
		(tv).tv_usec = (tv1).tv_usec - (tv2).tv_usec; \
		(tv).tv_sec = (tv1).tv_sec - (tv2).tv_sec;

#define tvplus(tv, tv1, tv2)	/* tv = tv1 + tv2 */ \
		(tv).tv_sec = (tv1).tv_sec + (tv2).tv_sec; \
		(tv).tv_usec = (tv1).tv_usec + (tv2).tv_usec; \
		if ((tv).tv_usec > 1000000) { \
			(tv).tv_usec -= 1000000; \
			(tv).tv_sec += 1; \
		}

/*-
 * TVTOMILLI(tv)
 *	Given a struct timeval, convert its time into milliseconds...
 */
#define TVTOMILLI(tv)	(((tv).tv_usec/1000)+((tv).tv_sec*1000))

extern int headsWanted, spreadHeads, group;
extern int XmonitorResolution, YmonitorResolution;

#define NATIVE_MONO	0
#define DMI_RESOLVER	1

#ifdef TIGA
extern Bool amixTCreate();
#endif /* TIGA */

#endif /* _AMIX_H_ */
