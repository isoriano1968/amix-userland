#!/bin/sh
# mkclips.sh -- synthetic test clips for the Phase 0 benchmarks, made with
# GStreamer on the PC: an animated zone plate (fine detail, constant motion)
# with a sine tone, 10 s at 25 fps.  Output in clips/.
set -e
cd "$(dirname "$0")"; mkdir -p clips
SRC="videotestsrc pattern=zone-plate kt=2 kx=3 ky=3 num-buffers=250"
AUD="audiotestsrc wave=sine freq=440 num-buffers=430 ! audio/x-raw,rate=44100,channels=2"
h264() {	# name width height profile kbit
	gst-launch-1.0 -q $SRC ! video/x-raw,width=$2,height=$3,framerate=25/1 ! videoconvert ! \
	  x264enc bitrate=$5 speed-preset=medium key-int-max=50 ! video/x-h264,profile=$4 ! h264parse ! \
	  mp4mux name=m ! filesink location=clips/$1.mp4 \
	  $AUD ! audioconvert ! avenc_aac bitrate=96000 ! m.
}
h264 h264-base-160x90    160 90  constrained-baseline 150
h264 h264-base-320x180   320 180 constrained-baseline 400
h264 h264-main-320x180   320 180 main                 400
h264 h264-base-480x270   480 270 constrained-baseline 800
gst-launch-1.0 -q $SRC ! video/x-raw,width=320,height=180,framerate=25/1 ! videoconvert ! \
  avenc_mpeg4 bitrate=400000 ! mpeg4videoparse ! avimux name=m ! filesink location=clips/mpeg4-320x180.avi \
  $AUD ! audioconvert ! lamemp3enc target=bitrate bitrate=96 ! mpegaudioparse ! m.
gst-launch-1.0 -q $SRC ! video/x-raw,width=320,height=180,framerate=25/1 ! videoconvert ! \
  avenc_mpeg1video bitrate=600000 ! mpegvideoparse ! avimux name=m ! filesink location=clips/mpeg1-320x180.avi \
  $AUD ! audioconvert ! lamemp3enc target=bitrate bitrate=96 ! mpegaudioparse ! m.
ls -l clips
