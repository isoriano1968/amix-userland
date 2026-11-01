# Motif 2.3.8 for AMIX: libXm and libMrm as shared libraries, static ones for
# what links them so, the headers, the .pc files and mwm.  In /usr/motif.
# The shared X of x11/x11r6.3 comes from X_STAGE, the result goes to SHARED_STAGE.
COMPONENT_NAME    = motif
COMPONENT_VERSION = 2.3.8
COMPONENT_ARCHIVE = motif-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://sourceforge.net/projects/motif/files/Motif%202.3.8%20Source%20Code/motif-2.3.8.tar.gz/download
COMPONENT_SHA256  = 859b723666eeac7df018209d66045c9853b50b4218cecadb794e2359619ebce7
CLEAN_FILES       = work $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

SHARED_STAGE ?= $(STAGE_DIR)
export X_STAGE ?= $(AMIX_SYSROOT)

build: $(COMPONENT_ARCHIVE)
	sh scripts/build-static.sh
	sh scripts/build-shared.sh $(SHARED_STAGE)
	sh scripts/package.sh

# the shared libraries and mwm are there from build
stage:
	mkdir -p $(SHARED_STAGE)/usr/motif/include
	cp -RL work/motif-root/usr/motif/include/. $(SHARED_STAGE)/usr/motif/include/
	chmod -R u+w,go+rX $(SHARED_STAGE)/usr/motif/include

.PHONY: build stage
