#!/bin/sh
# netsurf -- NetSurf in an X window on the AMIX display; a URL or a file may
# be given.  JavaScript (QuickJS-ng) is on; --enable_javascript=0 among the
# arguments turns it off again (NetSurf's options come first, the last wins).
# AMIX's default soft limits (16 MB data and memory) are too small for larger
# pages, their images and their scripts: 192 MB.
DISPLAY=${DISPLAY:-:0}; export DISPLAY
ulimit -d 196608 2>/dev/null; ulimit -v 196608 2>/dev/null
case "$1" in
/*)	set -- "file://$1" ;;		# a file dropped on the panel control
esac
exec /usr/local/bin/netsurf-fb --enable_javascript=1 "$@"
