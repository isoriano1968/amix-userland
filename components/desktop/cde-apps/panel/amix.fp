#
# amix.fp -- the AMIX control in CDE's front panel, with the AMIX logo: its
# subpanel holds AmixCast, the Z3660 window, the System Monitor, NEdit,
# fsv, NetSurf, Grafx2, Display, PyDPainter, AmixUAE, Printers and Quake III.  Every CDE application of ours gets a control here.  Installed in
# /etc/dt/appconfig/types/C in place of ztopcde.fp; dtwm reads it when the
# workspace manager starts (Restart Workspace Manager).
#
CONTROL Amix
{
	TYPE		icon
	CONTAINER_NAME	Top
	CONTAINER_TYPE	BOX
	POSITION_HINTS	20
	ICON		AmixPanel
	LABEL		AMIX
	PUSH_ACTION	AmixCast
	HELP_STRING	The A3000: AmixCast, and the Z3660 in the panel above
}

SUBPANEL AmixPanel
{
	CONTAINER_NAME	Amix
	TITLE		AMIX
}

CONTROL AmixCastPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	1
	ICON		AmixCast
	LABEL		AmixCast
	PUSH_ACTION	AmixCast
	HELP_STRING	Plays videos of the DLNA server
}

CONTROL Z3660
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	2
	ICON		Z3660
	LABEL		Z3660
	PUSH_ACTION	ZtopCDE
	HELP_STRING	The Z3660 board and its second ARM core
}

CONTROL SysMonPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	3
	ICON		SysMon
	LABEL		System Monitor
	PUSH_ACTION	AmixSysMon
	HELP_STRING	Processes, CPU, memory, swap, network and file systems
}

CONTROL NEditPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	4
	ICON		NEdit
	LABEL		NEdit
	PUSH_ACTION	NEdit
	DROP_ACTION	NEdit
	HELP_STRING	Text editor (developer/nedit)
}

CONTROL FsvPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	5
	ICON		Fsv
	LABEL		fsv
	PUSH_ACTION	Fsv
	DROP_ACTION	Fsv
	HELP_STRING	3D file system visualizer (graphics/fsv)
}

CONTROL NetSurfPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	6
	ICON		NetSurf
	LABEL		NetSurf
	PUSH_ACTION	NetSurf
	DROP_ACTION	NetSurf
	HELP_STRING	Web browser (web/netsurf)
}

CONTROL Grafx2Panel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	7
	ICON		Grafx2
	LABEL		Grafx2
	PUSH_ACTION	Grafx2
	DROP_ACTION	Grafx2
	HELP_STRING	Pixel art paint program in the spirit of Deluxe Paint (graphics/grafx2)
}

CONTROL DisplayPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	8
	ICON		Display
	LABEL		Display
	PUSH_ACTION	AmixDisplay
	HELP_STRING	Screen resolution, refresh and colours (amixdisplay)
}

CONTROL PyDPainterPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	9
	ICON		PyDPainter
	LABEL		PyDPainter
	PUSH_ACTION	PyDPainter
	DROP_ACTION	PyDPainter
	HELP_STRING	Deluxe Paint clone in Python 3 and pygame, slow to start (graphics/pydpainter)
}

CONTROL AmixUAEPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	10
	ICON		AmixUAE
	LABEL		Amiga
	PUSH_ACTION	AmixUAE
	DROP_ACTION	AmixUAE
	HELP_STRING	An Amiga 500/600/1200 on the Z3660's ARM core 1 (emulators/uae4arm): drop .adf or .hdf files here to boot them
}

CONTROL AmixPrintersPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	11
	ICON		AmixPrinters
	LABEL		Printers
	PUSH_ACTION	AmixPrinters
	HELP_STRING	Find network printers over UPnP and Bonjour, set them up for lp (amixprinters)
}

CONTROL AmixQ3Panel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	12
	ICON		AmixQ3
	LABEL		Quake III
	PUSH_ACTION	AmixQ3
	HELP_STRING	Quake III Arena (the demo) with the whole engine on the Z3660's ARM core 1 (games/ioquake3): picture in the overlay, sound on HDMI, F10 quits
}
