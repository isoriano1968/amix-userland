# Motif 2.3.8 for AMIX: libXm and libMrm as shared libraries, static ones for
# what links them so, the headers, the .pc files and mwm.  In /usr/motif.
# The shared X of x11/x11r6.3 comes from X_STAGE, the result goes to
# SHARED_STAGE (make-rules/shared.mk); the package takes usr/motif from there.
COMPONENT_NAME    = motif
COMPONENT_VERSION = 2.3.8
COMPONENT_ARCHIVE = motif-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://sourceforge.net/projects/motif/files/Motif%202.3.8%20Source%20Code/motif-2.3.8.tar.gz/download
COMPONENT_SHA256  = 859b723666eeac7df018209d66045c9853b50b4218cecadb794e2359619ebce7
CLEAN_FILES       = work $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export X_STAGE ?= $(AMIX_SYSROOT)

build: $(COMPONENT_ARCHIVE)
	sh scripts/build-static.sh
	sh scripts/build-shared.sh $(SHARED_STAGE)
	sh scripts/package.sh

# the shared libraries and mwm are there from build; the headers join them
M = $(SHARED_STAGE)/usr/motif
stage:
	mkdir -p $(M)/include
	cp -RL work/motif-root/usr/motif/include/. $(M)/include/
	chmod -R u+w,go+rX $(M)/include
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr
	cp -RP $(M) $(STAGE_DIR)/usr/
	$(call link_stubs,$(STAGE_DIR)/usr/motif/lib/lib*.so.4,/usr/motif/lib,$(STAGE_DIR)/usr/motif/lib/lib*.so.4 $(SHARED_STAGE)/usr/x11r6/lib/lib*.so.7)
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
