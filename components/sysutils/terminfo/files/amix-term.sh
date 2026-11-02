# amix-term.sh -- a TERM this system knows, whatever connected.  Sourced by
# /etc/profile: the closest match for an unknown one, with a note.
case "$TERM" in
"")	TERM=vt100 ;;
esac
if [ ! -f "/usr/share/lib/terminfo/`echo "$TERM" | cut -c1`/$TERM" ]; then
	case "$TERM" in
	*256color*|*kitty*|alacritty*|xterm*|rxvt*|konsole*|gnome*|vte*|foot*|st*|tmux*|screen*|wezterm*|ghostty*|contour*|iterm*)
		T=xterm ;;
	*)	T=vt100 ;;
	esac
	echo "TERM=$TERM is not known here, using $T"
	TERM=$T
fi
export TERM
