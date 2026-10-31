# Mesa 18.3 for the Z3660's core 1 and AMIX: softpipe with NEON fast paths.
COMPONENT_NAME    = mesa
COMPONENT_VERSION = 18.3.6
COMPONENT_ARCHIVE = mesa-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://archive.mesa3d.org/older-versions/18.x/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = aaf17638dcf5a90b93b6389e152fdc9ef147768b09598f24d2c5cf482fcfc705
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

# the patched source; core1-z3660-amix's build/xbuild.py compiles it
prep: $(COMPONENT_ARCHIVE)
	mkdir -p build
	tar xJf $(COMPONENT_ARCHIVE) -C build
	patch -s -p1 -d build/mesa-$(COMPONENT_VERSION) < patches/0001-amix-z3660.patch

.PHONY: prep
