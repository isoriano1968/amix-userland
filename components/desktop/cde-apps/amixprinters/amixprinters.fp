# amixprinters.fp -- AmixPrintersPanel in the AMIX subpanel of the front panel (desktop/cde-apps).
# In /etc/dt/appconfig/types/C; dtwm merges every .fp file of the
# types path.

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
