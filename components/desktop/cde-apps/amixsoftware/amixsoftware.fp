# amixsoftware.fp -- AmixSoftwarePanel in the AMIX subpanel of the front panel (desktop/cde-apps).
# In /etc/dt/appconfig/types/C; dtwm merges every .fp file of the
# types path.

CONTROL AmixSoftwarePanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	13
	ICON		AmixSoftware
	LABEL		Software Center
	PUSH_ACTION	AmixSoftware
	HELP_STRING	Find, install, upgrade and remove Amix software from the amigaux repository through apkg (amixsoftware); Try shows what a change would do
}
