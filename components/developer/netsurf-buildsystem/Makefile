# NetSurf's buildsystem 1.10: the shared makefiles NetSurf and its libraries
# build with, installed in $(PREFIX)/share/netsurf-buildsystem (the sysroot's
# copy serves the cross builds, the box's one a build on the Amiga).
COMPONENT_NAME    = buildsystem
COMPONENT_VERSION = 1.10
COMPONENT_ARCHIVE = buildsystem-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 3d3e39d569e44677c4b179129bde614c65798e2b3e6253160239d1fd6eae4d79
CLEAN_FILES       = buildsystem-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

S = buildsystem-$(COMPONENT_VERSION)

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build

stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C $(S) install PREFIX=$(PREFIX) DESTDIR=$(STAGE_DIR) INSTALL=install > build/install.log 2>&1
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' netsurf-buildsystem.pc.in \
		> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/netsurf-buildsystem.pc
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/netsurf-buildsystem-amix.tar usr

.PHONY: build stage
