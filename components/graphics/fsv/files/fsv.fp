# fsv.fp -- the Fsv control in the AMIX subpanel of the CDE front panel.
# Shipped by graphics/fsv itself, so the control is there only while the package
# is installed; dtwm merges every .fp file of the types path.

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
