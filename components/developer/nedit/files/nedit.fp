# nedit.fp -- the NEdit control in the AMIX subpanel of the CDE front panel.
# Shipped by developer/nedit itself, so the control is there only while the package
# is installed; dtwm merges every .fp file of the types path.

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
