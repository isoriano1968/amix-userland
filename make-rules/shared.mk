# What every component's Makefile includes.
AMIX_TOOLS ?= $(HOME)/opt/m68k-amix/bin
AMIX_CC    ?= $(AMIX_TOOLS)/m68k-elf-gcc

# libraries are found through pkg-config, in the sysroot, at their AMIX paths
AMIX_SYSROOT ?= $(HOME)/opt/m68k-amix-sysroot
PKG_CONFIG    = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools/amix-pkg-config)
export AMIX_SYSROOT

# out of the source tree, one per component; git ignores them
BUILD_DIR  = $(CURDIR)/build
STAGE_DIR  = $(CURDIR)/build/stage

# where a component belongs on AMIX: /usr/local, /usr/gnu, /usr/X, /usr
PREFIX     ?= /usr/local

# a manifest's requirements are checked before anything is built
ifneq ($(wildcard *.manifest),)
build: requires
endif
requires:
	$(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools/amix-requires) $(CURDIR)

clean:
	rm -rf build $(CLEAN_FILES)

# a component's stage target fills $(STAGE_DIR) with what goes on AMIX
sysroot-install: stage
	cp -R $(STAGE_DIR)/. $(AMIX_SYSROOT)/

.PHONY: clean sysroot-install requires

# the source: fetched into the component, checked against COMPONENT_SHA256
$(COMPONENT_ARCHIVE):
	curl -fL --retry 3 -o $@.part $(COMPONENT_URL)
	echo "$(COMPONENT_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@
