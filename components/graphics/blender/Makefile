# Blender 2.31a for AMIX, on x11/mesa-31.  No Python, no game engine.
COMPONENT_NAME    = blender
COMPONENT_VERSION = 2.31a
COMPONENT_ARCHIVE = blender-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://download.blender.org/source/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 8c05cfaa5206f46529fee25e0fcd320c0264be83b97f1167e618029faf3edc50

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

# the patched source in src/
prep: $(COMPONENT_ARCHIVE)
	mkdir -p src
	tar xjf $(COMPONENT_ARCHIVE) -C src --strip-components=1
	patch -s -p1 -d src < patches/0001-amix.patch

build:
	sh build-deps.sh
	python3 -I build.py
	sh link.sh

.PHONY: prep build
