# Xamix, the AMIX X server (X11R6.3, DDX hw/amix): one server for every
# graphics card, the cards are drivers in it (z3660 for the Z3660 RTG, zz9000
# for the MNT ZZ9000, cirrus for the Picasso II).  Built from the RTG fork's
# patches (patches/amix-z3660rtg, amix-z3660rtg 91667de) and the local
# patches.  Was x11/xzz9000 with the binary Xzz9000 until 2026-11-16.
# 2: card z3660 (/dev/z3660rtg), depths from RTGIOC_GETCAPS, -device.
# src, disk and sysroot-mirror hold AMIX material: links into private/, git ignores them.
COMPONENT_NAME    = xserver-amix
COMPONENT_VERSION = 2

include ../../../make-rules/shared.mk

build:
	sh build.sh

.PHONY: build
