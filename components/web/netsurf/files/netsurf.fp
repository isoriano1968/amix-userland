# netsurf.fp -- the NetSurf control in the AMIX subpanel of the CDE front panel.
# Shipped by web/netsurf itself, so the control is there only while the package
# is installed; dtwm merges every .fp file of the types path.

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
