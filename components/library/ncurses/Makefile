# ncurses 6.6 for AMIX: static, narrow characters, AMIX's terminfo first.
COMPONENT_NAME    = ncurses
COMPONENT_VERSION = 6.6
COMPONENT_ARCHIVE = ncurses-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftp.gnu.org/gnu/ncurses/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11
CLEAN_FILES       = ncurses-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build: $(COMPONENT_ARCHIVE)
	tar xzf $(COMPONENT_ARCHIVE)
	sh configure-amix.sh
	$(MAKE) -C build

.PHONY: build
