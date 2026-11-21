# FFmpeg with ffmpeg and ffplay, and vplay, for AMIX with the Z3660's core 1.
COMPONENT_NAME    = ffmpeg
COMPONENT_VERSION = 4.4.8
COMPONENT_ARCHIVE = ffmpeg-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://ffmpeg.org/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = c73848c4ae283d9eaee7be3b276affbc3543380483555500d0dd2c9b7e1c39c3

CLEAN_FILES       = build-ff build-ffplay build-host ffmpeg-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shmsys.mk

export PATH := $(AMIX_TOOLS):$(PATH)

ffmpeg-$(COMPONENT_VERSION)/configure: $(COMPONENT_ARCHIVE)
	tar xJf $(COMPONENT_ARCHIVE)
	touch $@

compat: ffmpeg-$(COMPONENT_VERSION)/configure
	sh compat/build.sh

build: compat
	sh configure-amix.sh
	$(MAKE) -C build-ff

# on the shared SDL 2 of library/sdl2 in the sysroot (make sysroot-install there)
ffplay: compat
	sh configure-amix.sh ffplay
	$(MAKE) -C build-ffplay
	../../../tools/amix-fix-needed build-ffplay/ffplay

# vplay links MIT-SHM statically: libshmsys.a (make-rules/shmsys.mk)
vplay: build
	$(SHMSYS_BUILD)
	SHM_LIBS=$(SHM_LIBS) sh build.sh
	../../../tools/amix-fix-needed vplay

# as on the box: the programs in /opt/ffmpeg/bin and /opt/vplay, their
# launchers (ulimit, core 1's player) in /opt/amix/bin
stage: build ffplay vplay
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/opt/ffmpeg/bin $(STAGE_DIR)/opt/vplay $(STAGE_DIR)/opt/amix/bin
	cp build-ff/ffmpeg build-ffplay/ffplay $(STAGE_DIR)/opt/ffmpeg/bin/
	cp vplay $(STAGE_DIR)/opt/vplay/vplay
	cp tools/ffmpeg.sh $(STAGE_DIR)/opt/amix/bin/ffmpeg
	cp tools/ffplay.sh $(STAGE_DIR)/opt/amix/bin/ffplay
	cp vplay.sh $(STAGE_DIR)/opt/amix/bin/vplay
	chmod 755 $(STAGE_DIR)/opt/ffmpeg/bin/* $(STAGE_DIR)/opt/vplay/vplay $(STAGE_DIR)/opt/amix/bin/*
	@$(manifest_files)

.PHONY: compat build ffplay vplay stage
