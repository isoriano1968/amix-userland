# SDL 2 for AMIX: a static libSDL2.a with Paula audio, pth threads and the zz9k renderer.
COMPONENT_NAME    = sdl2
COMPONENT_VERSION = 2.30.12
COMPONENT_ARCHIVE = SDL2-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/libsdl-org/SDL/releases/download/release-$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea

CLEAN_FILES       = SDL2-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

# SHM_LIBS: a directory with a static libXext.a with XShm, and libshmsys.a
build:
	sh fetch.sh
	sh build.sh

.PHONY: build
