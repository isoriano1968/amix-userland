# nsgenbind 0.9: NetSurf's binding generator, which turns the .bnd files of
# NetSurf's JavaScript (Duktape) handler into C.  A tool of the build machine,
# like gperf: built with its cc and installed in $(NSGENBIND_DIR), where
# web/netsurf finds it.  Nothing of it goes on AMIX.
COMPONENT_NAME    = nsgenbind
COMPONENT_VERSION = 0.9
COMPONENT_ARCHIVE = nsgenbind-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 232ce0f66cbc2c3eed6288ae26de2c567bbfbbc01d8b0f6fc6c1c1649d4b385d
CLEAN_FILES       = nsgenbind-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

S = nsgenbind-$(COMPONENT_VERSION)
# where it is installed on the build machine (web/netsurf's NSGENBIND_DIR)
NSGENBIND_DIR ?= $(HOME)/opt/nsgenbind
# flex and bison of the build machine; the buildsystem's makefiles from the sysroot
NSG_MAKE = $(MAKE) -C $(S) PREFIX=$(NSGENBIND_DIR) NSSHARED=$(AMIX_SYSROOT)$(PREFIX)/share/netsurf-buildsystem \
	CC=cc VARIANT=release

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	$(NSG_MAKE) > build/make.log 2>&1 || { grep -E ' error|Error' build/make.log | head -20; exit 1; }

# the build machine's copy, in $(NSGENBIND_DIR)/bin
host-install:
	$(NSG_MAKE) install > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	test -x $(NSGENBIND_DIR)/bin/nsgenbind

# nothing for AMIX: an empty stage
stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)

.PHONY: build stage host-install
