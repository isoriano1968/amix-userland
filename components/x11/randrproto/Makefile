# randrproto 1.1.2 for AMIX: the RandR extension headers (protocol 1.1), into
# /usr/x11r6/include/X11/extensions, with randrproto.pc.  Headers only: the R6.3
# Xlib takes the matching client library (x11/libxrandr) and the
# Xzz9000 server its own copy (x11/xzz9000 build.sh).
COMPONENT_NAME    = randrproto
COMPONENT_VERSION = 1.1.2
COMPONENT_ARCHIVE = randrproto-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://www.x.org/releases/individual/proto/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 00a8c8af96097ed831a6f02c138c1a04a0226b64e446b4d7c824930dc43eb98d
PREFIX            = /usr/x11r6
S                 = randrproto-$(COMPONENT_VERSION)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

build: $(COMPONENT_ARCHIVE)
	$(unpack)

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/include/X11/extensions $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	cp $(S)/randr.h $(S)/randrproto.h $(STAGE_DIR)$(PREFIX)/include/X11/extensions/
	sed -e 's|@prefix@|$(PREFIX)|; s|@exec_prefix@|$${prefix}|; s|@libdir@|$${exec_prefix}/lib|' \
	    -e 's|@includedir@|$${prefix}/include|; s|@PACKAGE_VERSION@|$(COMPONENT_VERSION)|' \
	    $(S)/randrproto.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/randrproto.pc
	$(manifest_files)
	$(stage_finish)

.PHONY: build stage
