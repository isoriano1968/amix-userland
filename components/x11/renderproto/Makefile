# renderproto 0.9.2 for AMIX: the Render extension headers (protocol 0.9), into
# /usr/x11r6/include/X11/extensions, with renderproto.pc.  Headers only: the R6.3
# Xlib takes the matching client library (x11/libxrender) and the
# Xzz9000 server its own copy (x11/xzz9000 build.sh).
COMPONENT_NAME    = renderproto
COMPONENT_VERSION = 0.9.2
COMPONENT_ARCHIVE = renderproto-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://www.x.org/releases/individual/proto/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 7754dfbbb1e3818f28720fa9647b8a593e9d54626a7896771c2bfac7f75e2726
PREFIX            = /usr/x11r6
S                 = renderproto-$(COMPONENT_VERSION)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

build: $(COMPONENT_ARCHIVE)
	$(unpack)

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/include/X11/extensions $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	cp $(S)/render.h $(S)/renderproto.h $(STAGE_DIR)$(PREFIX)/include/X11/extensions/
	sed -e 's|@prefix@|$(PREFIX)|; s|@exec_prefix@|$${prefix}|; s|@libdir@|$${exec_prefix}/lib|' \
	    -e 's|@includedir@|$${prefix}/include|; s|@PACKAGE_VERSION@|$(COMPONENT_VERSION)|' \
	    $(S)/renderproto.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/renderproto.pc
	$(manifest_files)
	$(stage_finish)

.PHONY: build stage
