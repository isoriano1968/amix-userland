# amixdisplay.fp -- DisplayPanel in the AMIX subpanel of the front panel (desktop/cde-apps).
# In /etc/dt/appconfig/types/C; dtwm merges every .fp file of the
# types path.

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
