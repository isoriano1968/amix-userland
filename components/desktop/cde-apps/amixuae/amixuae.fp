# amixuae.fp -- AmixUAEPanel in the AMIX subpanel of the front panel (desktop/cde-amixuae).
# In /etc/dt/appconfig-hw/z3660-core1/types/C: CDE reads it only when
# amix-hw finds the Z3660 (Xsession.d/0013.amix-hw), so the control shows
# only on a machine with the board.

CONTROL AmixUAEPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	10
	ICON		AmixUAE
	LABEL		Amiga
	PUSH_ACTION	AmixUAE
	DROP_ACTION	AmixUAE
	HELP_STRING	An Amiga 500/600/1200 on the Z3660's ARM core 1 (emulators/uae4arm): drop .adf or .hdf files here to boot them
}
