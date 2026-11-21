# The Halloween intro for AMIX: every pixel on the Z3660's core 1, which needs a
# gpu core with zzgl's ZZ_EFFECT (amix-gate firmware/core1/zzgl/spook.c).
# The module is not here: Halloween Bass by dilik, from The Mod Archive
# (module 208024), as halloween_bass.it beside the program.
COMPONENT_NAME    = halloween
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

# amix-gate beside amix-userland (the Build tab and the rebuild clone it there);
# lib060's archives and mesa-183's GL headers from the sysroot
GATE = $(abspath ../../../../amix-gate)/usr/src/firmware/core1
L060 = $(AMIX_SYSROOT)/usr/local/lib
X    = $(AMIX_SYSROOT)/usr/x11r6
CC   = $(AMIX_TOOLS)/m68k-elf-gcc
CFLAGS = -O2 -DNDEBUG -m68060 -ffast-math -fno-strict-aliasing -fcommon -w -U__linux__ -Ulinux \
	-D__amix__ -DSVR4 -I$(GATE)/zzgl -I$(AMIX_SYSROOT)/usr/local/include -I$(GATE)/compat/amix \
	-I$(GATE)/cores/h264bench -I$(X)/include

# libmikmod with its HDMI driver comes from the sysroot: audio/libmikmod's sysroot-install
build:
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) `$(PKG_CONFIG) --cflags libmikmod` -c halloween.c -o $(BUILD_DIR)/halloween.o
	$(CC) -m68060 -Wl,--allow-multiple-definition -o $(BUILD_DIR)/halloween $(BUILD_DIR)/halloween.o \
		`$(PKG_CONFIG) --libs libmikmod zzgl x11` -lsocket -lnsl -L$(L060) -lm060 -lc060 -lm

.PHONY: build
