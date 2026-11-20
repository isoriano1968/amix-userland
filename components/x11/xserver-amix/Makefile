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

XS    = src/xc/programs/Xserver
STRIP = $(HOME)/opt/amix-cross/bin/m68k-cbm-sysv4-strip
X11   = $(STAGE_DIR)/usr/x11r6

build:
	sh build.sh

# the server, the X link, rgb.txt (the colour names, UseRgbTxt), the SECURITY extension's policy (the server reads
# /usr/x11r6/lib/X11/xserver/SecurityPolicy at start and complains without
# it) and the server's manual page (Xserver(1): the DIX
# options).  hw/amix/Xamix.man is not installed: it is still Xsun's page with
# the name replaced (Sun frame buffers, none of -card, -mode, -rtgdepth).
# The dtlogin wrapper Xamix-dt is CDE's (desktop/cde files/dtlogin).
stage: $(XS)/Xamix
	rm -rf $(STAGE_DIR)
	mkdir -p $(X11)/bin $(X11)/man/man1 $(X11)/lib/X11/xserver
	$(STRIP) -o $(X11)/bin/Xamix $(XS)/Xamix
	chmod 755 $(X11)/bin/Xamix
	ln -s Xamix $(X11)/bin/X
	cp src/xc/programs/rgb/rgb.txt $(X11)/lib/X11/rgb.txt
	chmod 644 $(X11)/lib/X11/rgb.txt
	cp $(XS)/Xserver.man $(X11)/man/man1/Xserver.1
	chmod 644 $(X11)/man/man1/Xserver.1
	cp $(XS)/Xext/SecurityPolicy $(X11)/lib/X11/xserver/SecurityPolicy
	chmod 644 $(X11)/lib/X11/xserver/SecurityPolicy
	chmod -R go-w,a+rX $(STAGE_DIR)
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' xserver-amix.manifest`" || \
		{ echo "the stage and xserver-amix.manifest list different files"; exit 1; }
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

$(XS)/Xamix:
	sh build.sh

.PHONY: build stage
