# amixsysmon.fp -- SysMonPanel in the AMIX subpanel of the front panel (desktop/cde-apps).
# In /etc/dt/appconfig/types/C; dtwm merges every .fp file of the
# types path.

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
