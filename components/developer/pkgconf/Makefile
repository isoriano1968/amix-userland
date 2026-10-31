# pkgconf 2.3.0 for AMIX: pkg-config on the machine itself.  Static.
#   make build             cross-built into build/
#   make sysroot-install   /usr/local/bin/pkg-config in the sysroot
# Cross builds on the PC use tools/amix-pkg-config instead.
COMPONENT_NAME    = pkgconf
COMPONENT_VERSION = 2.3.0
COMPONENT_ARCHIVE = pkgconf-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://distfiles.ariadne.space/pkgconf/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 3a9080ac51d03615e7c1910a0a2a8df08424892b5f13b0628a204d3fcce0ea8b
CLEAN_FILES       = pkgconf-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build: $(COMPONENT_ARCHIVE)
	rm -rf pkgconf-$(COMPONENT_VERSION)
	tar xf $(COMPONENT_ARCHIVE)
	patch -d pkgconf-$(COMPONENT_VERSION) -p1 < patches/01-size-format.patch
	sh configure-amix.sh
	$(MAKE) -C build pkgconf

stage:
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin
	cp $(BUILD_DIR)/pkgconf $(STAGE_DIR)$(PREFIX)/bin/pkg-config
	m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/pkg-config

.PHONY: build stage
