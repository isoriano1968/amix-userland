# Xamix, the AMIX X server (X11R6.3, DDX hw/amix): one server for every
# graphics card, the cards are drivers in it (zz9000 for the ZZ9000 and the
# Z3660, cirrus for the Picasso II).  Built from the RTG fork and the local
# patches.  Was x11/xzz9000 with the binary Xzz9000 until 2026-11-16.
# src, disk and sysroot-mirror hold AMIX material: links into private/, git ignores them.
COMPONENT_NAME    = xserver-amix
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

build:
	sh build.sh

.PHONY: build
