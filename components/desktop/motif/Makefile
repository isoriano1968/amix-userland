# Motif 2.3.8 for AMIX: libXm and libMrm as shared libraries (libXm.so.4,
# libMrm.so.4), their headers and .pc files, and mwm with its
# configuration, in /usr/motif.
#
#   gmake AMIX_SYSROOT=/path/to/sysroot stage
#
# Inputs, all pinned:
#   motif-2.3.8.tar.gz       from SourceForge (COMPONENT_SHA256)
#   patches/*.patch          the AMIX changes
#   files/motif-amix.cache   configure's answers for AMIX (no test programs)
#   AMIX_SYSROOT             a clean sysroot with x11/x11r6.3 in it:
#                            developer/amix/sysroot + developer/amix/x11r6.3 on
#                            OpenIndiana; amix-requires installs x11r6.3 into
#                            a local one.  Never one that already holds a
#                            /usr/motif.
#
# Tools: gcc-14 (AMIX_TOOLS) compiles and links everything; host cc, flex and
# the host's X11 headers build Motif's own tools (makestrs, wml); GNU make.
COMPONENT_NAME    = motif
COMPONENT_VERSION = 2.3.8
COMPONENT_ARCHIVE = motif-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://sourceforge.net/projects/motif/files/Motif%202.3.8%20Source%20Code/motif-2.3.8.tar.gz/download
COMPONENT_SHA256  = 859b723666eeac7df018209d66045c9853b50b4218cecadb794e2359619ebce7
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

AMIX_CPU ?= -m68060
HOST_CC  ?= gcc
export AMIX_SYSROOT AMIX_TOOLS AMIX_CPU HOST_CC

$(COMPONENT_ARCHIVE):
	curl -fsL -o $@.tmp '$(COMPONENT_URL)'
	echo "$(COMPONENT_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@

# 1. sources and host tools
$(BUILD_DIR)/.prep: $(COMPONENT_ARCHIVE) scripts/prep.sh scripts/common.sh $(wildcard patches/*.patch)
	echo "$(COMPONENT_SHA256)  $(COMPONENT_ARCHIVE)" | sha256sum -c --quiet -
	test ! -d "$(AMIX_SYSROOT)/usr/motif" || \
		{ echo "$(AMIX_SYSROOT) already holds usr/motif: build against a clean sysroot"; exit 1; }
	mkdir -p $(BUILD_DIR)
	MAKE=$(MAKE) sh scripts/prep.sh
	touch $@

# 2. the shared libraries, headers, .pc files and mwm
$(BUILD_DIR)/.built: $(BUILD_DIR)/.prep scripts/build-shared.sh files/motif-amix.cache
	MAKE=$(MAKE) sh scripts/build-shared.sh $(BUILD_DIR)/shared > $(BUILD_DIR)/build-shared.log 2>&1 || \
		{ tail -20 $(BUILD_DIR)/build-shared.log; exit 1; }
	touch $@

prep: $(BUILD_DIR)/.prep
build: $(BUILD_DIR)/.built

# 3. the stage: /usr/motif, mwm's app-defaults for R6.3's Xt, link stubs
M = $(BUILD_DIR)/shared/usr/motif
stage: build
	rm -rf "$(STAGE_DIR)"
	mkdir -p $(STAGE_DIR)/usr $(STAGE_DIR)/usr/x11r6/lib/X11/app-defaults
	cp -RP $(M) $(STAGE_DIR)/usr/
	cp files/Mwm $(STAGE_DIR)/usr/x11r6/lib/X11/app-defaults/Mwm
	$(call link_stubs,$(STAGE_DIR)/usr/motif/lib/lib*.so.4,/usr/motif/lib,$(STAGE_DIR)/usr/motif/lib/lib*.so.4 $(AMIX_SYSROOT)/usr/x11r6/lib/lib*.so.7)
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' motif.manifest`" || \
		{ echo "the stage and motif.manifest list different files"; exit 1; }
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: prep build stage
