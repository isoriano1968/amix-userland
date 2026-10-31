# GNU fileutils 4.1 for AMIX (ls and the rest), cross-built with developer/gcc-14.
COMPONENT_NAME    = fileutils
COMPONENT_VERSION = 4.1
COMPONENT_ARCHIVE = fileutils-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftp.gnu.org/old-gnu/fileutils/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 301c60795b011e63f034cfdf7b327b60f1f8f76909337a530b903bb697937188
CLEAN_FILES       = fileutils-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build: $(COMPONENT_ARCHIVE)
	tar xzf $(COMPONENT_ARCHIVE)
	sh configure-amix.sh
	$(MAKE) -C fileutils-$(COMPONENT_VERSION)/lib
	# getopt: its own and the one in AMIX's libc
	$(MAKE) -C fileutils-$(COMPONENT_VERSION)/src ls LDFLAGS=-Wl,--allow-multiple-definition

.PHONY: build
