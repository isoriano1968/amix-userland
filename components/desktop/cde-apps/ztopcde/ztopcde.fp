# ztopcde.fp -- Z3660 in the AMIX subpanel of the front panel (desktop/cde-z3660).
# In /etc/dt/appconfig-hw/z3660-core1/types/C: CDE reads it only when
# amix-hw finds the Z3660 (Xsession.d/0013.amix-hw), so the control shows
# only on a machine with the board.

CONTROL Z3660
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	2
	ICON		Z3660
	LABEL		Z3660
	PUSH_ACTION	ZtopCDE
	HELP_STRING	The Z3660 board and its second ARM core
}
