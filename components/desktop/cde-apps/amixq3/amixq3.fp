# amixq3.fp -- AmixQ3Panel in the AMIX subpanel of the front panel (desktop/cde-amixq3).
# In /etc/dt/appconfig-hw/z3660-core1/types/C: CDE reads it only when
# amix-hw finds the Z3660 (Xsession.d/0013.amix-hw), so the control shows
# only on a machine with the board.

CONTROL AmixQ3Panel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	12
	ICON		AmixQ3
	LABEL		Quake III / RtCW
	PUSH_ACTION	AmixQ3
	HELP_STRING	Quake III Arena or Return to Castle Wolfenstein with the whole engine on the Z3660's ARM core 1 (games/ioquake3, games/iortcw): picture in the overlay, sound on HDMI, F10 quits
}
