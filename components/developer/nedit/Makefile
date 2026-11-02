# NEdit 5.7 for AMIX: a Motif editor with syntax highlighting (C, Makefile,
# GLSL) and a Shell menu that runs make.  nedit and nc in /usr/local/bin.
COMPONENT_NAME    = nedit
COMPONENT_VERSION = 5.7
COMPONENT_ARCHIVE = nedit-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://downloads.sourceforge.net/project/nedit/nedit-source/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = add9ac79ff973528ad36c86858238bac4f59896c27dbf285cbe6a4d425fca17a
CLEAN_FILES       = nedit-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = nedit-$(COMPONENT_VERSION)
DT = $(STAGE_DIR)/etc/dt/appconfig
export XCFLAGS := $(shell $(PKG_CONFIG) --cflags xm xt x11)
export XLIBS := $(shell $(PKG_CONFIG) --libs xm xt x11) -lgen -lsocket -lnsl -lm

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S)
	tar xzf $(COMPONENT_ARCHIVE)
	cp files/Makefile.amix $(S)/makefiles/
	-$(MAKE) -C $(S) amix    # its last step runs the program it built
	mkdir -p build
	cp $(S)/source/nedit $(S)/source/nc build/
	m68k-elf-strip build/nedit build/nc

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(DT)/types/C $(DT)/icons/C $(DT)/appmanager/C/Desktop_Apps
	cp build/nedit build/nc $(STAGE_DIR)$(PREFIX)/bin/
	cp files/nedit.dt $(DT)/types/C/
	python3 -I files/mkicons.py $(DT)/icons/C
	: > $(DT)/appmanager/C/Desktop_Apps/NEdit
	chmod 755 $(DT)/appmanager/C/Desktop_Apps/NEdit
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/nedit-amix.tar etc usr

.PHONY: build stage
