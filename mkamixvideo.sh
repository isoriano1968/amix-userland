#!/bin/sh
# mkamixvideo.sh -- convert any video into what vplay plays well on a 68060:
# MPEG-4 Part 2 at 320 pixels wide (height by the aspect ratio, even),
# 12.5 frames a second, MP2 sound at 22050 Hz mono, in an AVI file.
#
#   mkamixvideo.sh [-w width] [-fps rate] [-vb kbit] in.mp4 [out.avi]
#
# Measured on an A3000 with a Z3660 (68060, 80 MHz): MPEG-4 at 320x180 with
# MP2 sound plays at about 11 frames a second, so 12.5 is what it shows in
# full.  H.264 costs twice as much to decode as MPEG-4 Part 2, AAC and MP3
# more than MP2.  Uses ffmpeg when it is installed, otherwise GStreamer.
set -e
W=320 FPS=12.5 VB=400
while [ $# -gt 0 ]; do
	case $1 in
	-w) W=$2; shift 2 ;;
	-fps) FPS=$2; shift 2 ;;
	-vb) VB=$2; shift 2 ;;
	-*) echo "usage: $0 [-w width] [-fps rate] [-vb kbit] in [out.avi]"; exit 2 ;;
	*) break ;;
	esac
done
[ $# -ge 1 ] || { echo "usage: $0 [-w width] [-fps rate] [-vb kbit] in [out.avi]"; exit 2; }
IN=$1
OUT=${2:-${IN%.*}-amix.avi}
case $FPS in
*.*) FPSN=$(echo "$FPS" | awk '{printf "%d/10", $1 * 10}') ;;
*) FPSN=$FPS/1 ;;
esac

if command -v ffmpeg >/dev/null 2>&1; then
	exec ffmpeg -hide_banner -i "$IN" -vf "scale=$W:-2,fps=$FPS" \
	  -c:v mpeg4 -b:v ${VB}k -g 25 -bf 0 \
	  -c:a mp2 -ar 22050 -ac 1 -b:a 64k "$OUT"
fi
command -v gst-launch-1.0 >/dev/null 2>&1 || { echo "$0: needs ffmpeg or gst-launch-1.0"; exit 1; }
case $IN in /*) URI=file://$IN ;; *) URI=file://$(pwd)/$IN ;; esac
INFO=$(gst-discoverer-1.0 "$URI" 2>/dev/null)
# the height that keeps the aspect ratio, rounded to even
H=$(echo "$INFO" | awk -v w=$W '
	/Width:/ { iw = $2 } /Height:/ { ih = $2 }
	END { if (iw && ih) { h = int(w * ih / iw / 2 + 0.5) * 2; print h } else print int(w * 9 / 16 / 2) * 2 }')
AUDIO=
SND="no sound"
if echo "$INFO" | grep -q 'audio #'; then
	AUDIO="d. ! queue ! audioconvert ! audioresample ! audio/x-raw,rate=22050,channels=1 !
	  avenc_mp2 bitrate=64000 ! mpegaudioparse ! m."
	SND="MP2 22050 Hz mono"
fi
gst-launch-1.0 -q uridecodebin uri="$URI" name=d \
  d. ! queue ! videoconvert ! videoscale ! videorate ! \
    video/x-raw,format=I420,width=$W,height=$H,framerate=$FPSN ! \
    avenc_mpeg4 bitrate=$((VB * 1000)) gop-size=25 max-bframes=0 ! mpeg4videoparse ! avimux name=m ! \
    filesink location="$OUT" $AUDIO
echo "$OUT: ${W}x$H, $FPS fps, MPEG-4, $SND"
