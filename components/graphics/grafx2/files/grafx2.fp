# grafx2.fp -- the Grafx2 control in the AMIX subpanel of the CDE front panel.
# Shipped by graphics/grafx2 itself, so the control is there only while the package
# is installed; dtwm merges every .fp file of the types path.

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
