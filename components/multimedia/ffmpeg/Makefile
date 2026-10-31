# FFmpeg with ffmpeg and ffplay, and vplay, for AMIX with the Z3660's core 1.
COMPONENT_NAME    = ffmpeg
COMPONENT_VERSION = 4.4.8
COMPONENT_ARCHIVE = ffmpeg-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://ffmpeg.org/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = c73848c4ae283d9eaee7be3b276affbc3543380483555500d0dd2c9b7e1c39c3

CLEAN_FILES       = build-ff build-ffplay build-host ffmpeg-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

ffmpeg-$(COMPONENT_VERSION)/configure: $(COMPONENT_ARCHIVE)
	tar xJf $(COMPONENT_ARCHIVE)
	touch $@

compat: ffmpeg-$(COMPONENT_VERSION)/configure
	sh compat/build.sh

build: compat
	sh configure-amix.sh
	$(MAKE) -C build-ff

# on SDL 2: build library/sdl2 first
ffplay: compat
	sh configure-amix.sh ffplay
	$(MAKE) -C build-ffplay

vplay: build
	sh build.sh

.PHONY: compat build ffplay vplay
