#!/bin/sh
# screenshots.sh DIR [PROGRAM] -- render every step, light and dark, under Xvfb
# (PROGRAM: an installed amix-installer; default the source tree).
# Starts its own Xvfb on a free display and stops it (by pid) afterwards;
# never uses $DISPLAY.
out=${1:?usage: screenshots.sh DIR [PROGRAM]}
here=$(cd "$(dirname "$0")/.." && pwd)
prog=${2:-$here/amix-installer}
command -v Xvfb >/dev/null || { echo "screenshots: no Xvfb" >&2; exit 1; }
n=77
while [ -e /tmp/.X11-unix/X$n ] || [ -e /tmp/.X$n-lock ]; do n=$((n + 1)); done
Xvfb :$n -screen 0 1280x900x24 -nolisten tcp >/dev/null 2>&1 &
xpid=$!
trap 'kill $xpid 2>/dev/null' EXIT INT TERM
i=0
while [ ! -e /tmp/.X11-unix/X$n ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
mkdir -p "$out"
# no session bus, no portal: file dialogs are not used here
unset WAYLAND_DISPLAY
env DISPLAY=:$n GDK_BACKEND=x11 GSK_RENDERER=cairo \
    NO_AT_BRIDGE=1 GTK_A11Y=none "$prog" --screenshot "$out" || exit 1
env DISPLAY=:$n GDK_BACKEND=x11 GSK_RENDERER=cairo \
    NO_AT_BRIDGE=1 GTK_A11Y=none AMIX_SHOT_DARK=1 "$prog" --screenshot "$out"
