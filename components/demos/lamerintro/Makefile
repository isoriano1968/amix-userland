# LamerIntro for AMIX: the GL on the Z3660's core 1 (amix-gate firmware/core1).
# assets/ is made by mkassets.py from the original's textures and font.
COMPONENT_NAME    = lamerintro
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

# amix-gate beside amix-userland (the Build tab and the rebuild clone it there);
# lib060's archives and mesa-183's GL headers from the sysroot
GATE = $(abspath ../../../../amix-gate)/usr/src/firmware/core1
L060 = $(AMIX_SYSROOT)/usr/local/lib
X    = $(AMIX_SYSROOT)/usr/x11r6
CC   = $(AMIX_TOOLS)/m68k-elf-gcc
CFLAGS = -O2 -DNDEBUG -m68060 -ffast-math -fno-strict-aliasing -fcommon -w -U__linux__ -Ulinux \
	-D__amix__ -DSVR4 -fno-builtin-sin -fno-builtin-cos -fno-builtin-tan -fno-builtin-fmod \
	-fno-builtin-sinf -fno-builtin-cosf -Iassets -I$(GATE)/zzgl -I$(AMIX_SYSROOT)/usr/local/include -I$(GATE)/compat/amix -I$(GATE)/cores/h264bench -I$(X)/include

# libmikmod with its HDMI driver comes from the sysroot: audio/libmikmod's sysroot-install
build:
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) `$(PKG_CONFIG) --cflags libmikmod` -c lamerintro.c -o $(BUILD_DIR)/lamerintro.o
	$(CC) -m68060 -Wl,--allow-multiple-definition -o $(BUILD_DIR)/lamerintro $(BUILD_DIR)/lamerintro.o \
		`$(PKG_CONFIG) --libs libmikmod zzgl x11` -lsocket -lnsl -L$(L060) -lm060 -lc060 -lm

# the stage: the program and its launcher in /opt/lamerintro (the music goes beside
# them, it is not part of the package)
stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/opt/lamerintro
	cp $(BUILD_DIR)/lamerintro lamerintro.sh $(STAGE_DIR)/opt/lamerintro/
	$(AMIX_TOOLS)/m68k-elf-strip $(STAGE_DIR)/opt/lamerintro/lamerintro
	chmod 755 $(STAGE_DIR)/opt/lamerintro/*
	@$(manifest_files)

.PHONY: build stage
