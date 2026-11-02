#
# amix.fp -- the AMIX control in CDE's front panel, with the AMIX logo: its
# subpanel holds the Z3660 window, AmixCast, NEdit and fsv.  Installed in
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

CONTROL NEditPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	3
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
	POSITION_HINTS	4
	ICON		Fsv
	LABEL		fsv
	PUSH_ACTION	Fsv
	DROP_ACTION	Fsv
	HELP_STRING	3D file system visualizer (graphics/fsv)
}
