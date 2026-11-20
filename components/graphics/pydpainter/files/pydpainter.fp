# pydpainter.fp -- the PyDPainter control in the AMIX subpanel of the CDE front panel.
# Shipped by graphics/pydpainter itself, so the control is there only while the package
# is installed; dtwm merges every .fp file of the types path.

CONTROL PyDPainterPanel
{
	TYPE		icon
	CONTAINER_NAME	AmixPanel
	CONTAINER_TYPE	SUBPANEL
	POSITION_HINTS	9
	ICON		PyDPainter
	LABEL		PyDPainter
	PUSH_ACTION	PyDPainter
	DROP_ACTION	PyDPainter
	HELP_STRING	Deluxe Paint clone in Python 3 and pygame, slow to start (graphics/pydpainter)
}
