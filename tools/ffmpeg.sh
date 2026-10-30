#!/bin/sh
# ffmpeg -- FFmpeg 4.4.8 (build-ff) on the A3000.  -nostdin always: without it
# ffmpeg leaves the (serial) terminal in raw 8-bit mode.
#   installed as /opt/amix/bin/ffmpeg, the binary in /opt/ffmpeg/bin
ulimit -d 98304 2>/dev/null; ulimit -v 98304 2>/dev/null
exec /opt/ffmpeg/bin/ffmpeg -nostdin "$@"
