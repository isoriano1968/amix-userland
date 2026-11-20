#
# amix.fp -- the AMIX control in CDE's front panel, with the AMIX logo, and
# its subpanel AmixPanel.  The subpanel holds no applications of its own:
# every package with a CDE application ships its control into AmixPanel as
# <name>.fp (CONTAINER_NAME AmixPanel, CONTAINER_TYPE SUBPANEL), so the
# subpanel shows what is installed, and what needs a board (the Z3660's
# core 1) only when the board is there (/etc/dt/appconfig-hw, amix-hw).
# Installed in /etc/dt/appconfig/types/C; dtwm reads it when the workspace
# manager starts (Restart Workspace Manager).
#
CONTROL Amix
{
	TYPE		icon
	CONTAINER_NAME	Top
	CONTAINER_TYPE	BOX
	POSITION_HINTS	20
	ICON		AmixPanel
	LABEL		AMIX
	PUSH_ACTION	AmixSoftware
	HELP_STRING	AMIX: the Software Center, and the AMIX applications in the panel above
}

SUBPANEL AmixPanel
{
	CONTAINER_NAME	Amix
	TITLE		AMIX
}
