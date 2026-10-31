#!/bin/sh
# build-host.sh -- vplay for Linux, against build-host/'s FFmpeg, for testing
# the player in an Xephyr window:  Xephyr :5 -screen 1024x768x16 &
#   DISPLAY=:5 ./vplay-host -noaudio clips/h264-base-320x180.mp4
set -e
V=$(cd "$(dirname "$0")" && pwd); B=$V/build-host
cc -O2 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -I$V/ffmpeg-4.4.8 -I$B -o $V/vplay-host $V/src/vplay.c \
  $B/libavformat/libavformat.a $B/libavcodec/libavcodec.a $B/libswresample/libswresample.a \
  $B/libswscale/libswscale.a $B/libavutil/libavutil.a -lXext -lX11 -lm
