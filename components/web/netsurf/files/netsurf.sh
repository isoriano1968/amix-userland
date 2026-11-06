#!/bin/sh
# netsurf -- NetSurf in an X window on the AMIX display; a URL or a file may
# be given.  AMIX's default soft limits (16 MB data and memory) are too small
# for larger pages and their images.
DISPLAY=${DISPLAY:-:0}; export DISPLAY
ulimit -d 98304 2>/dev/null; ulimit -v 98304 2>/dev/null
case "$1" in
/*)	set -- "file://$1" ;;		# a file dropped on the panel control
esac
exec /usr/local/bin/netsurf-fb "$@"
