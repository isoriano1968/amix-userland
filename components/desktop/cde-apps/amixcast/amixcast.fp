# amixcast.fp -- AmixCastPanel in the AMIX subpanel of the front panel (desktop/cde-z3660).
# In /etc/dt/appconfig-hw/z3660-core1/types/C: CDE reads it only when
# amix-hw finds the Z3660 (Xsession.d/0013.amix-hw), so the control shows
# only on a machine with the board.

CONTROL AmixCastPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	1
	ICON		AmixCast
	LABEL		AmixCast
	PUSH_ACTION	AmixCast
	HELP_STRING	Plays videos of the DLNA server
}
