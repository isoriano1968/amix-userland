# libmikmod 3.1.23 and the mikmod 3.2.9 player for AMIX, in libmikmod/ and mikmod/.
#   make build              the shared library for the 68060 into build/, with the
#                           HDMI driver (needs amix-gate beside amix-userland, for
#                           the board's numbers)
#   make sysroot-install    the library into the sysroot, with libmikmod.pc
#   make player             mikmod, linked through pkg-config
# On the AMIX machine with its gcc, without HDMI: make in each directory
# (see README.md).
COMPONENT_NAME    = libmikmod
COMPONENT_VERSION = 3.1.23

include ../../../make-rules/shared.mk

GATE = $(abspath ../../../../amix-gate/usr/src/firmware/core1)
BASE = -O2 -m68060 -fcommon -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
       -Wno-error=incompatible-pointer-types -D__amix__ -DSVR4 -DHAVE_CONFIG_H -Dunix

export PATH := $(AMIX_TOOLS):$(PATH)

build:
	mkdir -p $(BUILD_DIR)
	cp -R libmikmod mikmod $(BUILD_DIR)/
	mkdir -p $(BUILD_DIR)/pic
	cp -R libmikmod $(BUILD_DIR)/pic/
	$(MAKE) -C $(BUILD_DIR)/pic/libmikmod libmikmod.so.3 CC=m68k-elf-gcc \
		CFLAGS='$(BASE) -fPIC -DDRV_HDMI -I. -Iinclude -I$(GATE)/zzgl -I$(GATE)/cores/h264bench'
	../../../tools/amix-got-check $(BUILD_DIR)/pic/libmikmod/*/*.o

# the shared library where it belongs on AMIX
stage:
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp $(BUILD_DIR)/pic/libmikmod/libmikmod.so.3 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libmikmod.so.3 $(STAGE_DIR)$(PREFIX)/lib/libmikmod.so
	cp libmikmod/include/mikmod.h libmikmod/include/mikmod_ctype.h $(STAGE_DIR)$(PREFIX)/include/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' libmikmod.pc.in \
		> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libmikmod.pc

# the player, from the library installed in the sysroot
player:
	$(MAKE) -C $(BUILD_DIR)/mikmod mikmod CC=m68k-elf-gcc \
		CFLAGS='$(BASE) -I. -Isrc `$(PKG_CONFIG) --cflags libmikmod ncurses`' \
		LDFLAGS='`$(PKG_CONFIG) --libs libmikmod ncurses` -lm'
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin
	cp $(BUILD_DIR)/mikmod/mikmod $(STAGE_DIR)$(PREFIX)/bin/

.PHONY: build stage player
