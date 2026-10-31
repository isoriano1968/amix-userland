# LamerIntro for AMIX: the GL on the Z3660's core 1 (amix-gate firmware/core1).
# assets/ is made by mkassets.py from the original's textures and font.
COMPONENT_NAME    = lamerintro
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

GATE = $(HOME)/Sources/usr-amix/amix-gate/usr/src/firmware/core1
M    = $(HOME)/Sources/usr-amix/amix-userland/components/audio/libmikmod/libmikmod
L060 = $(HOME)/Sources/usr-amix/amix-userland/components/library/lib060
X    = $(HOME)/opt/m68k-amix-sysroot/usr/x11r6/xc/exports
CC   = $(AMIX_TOOLS)/m68k-elf-gcc
CFLAGS = -O2 -DNDEBUG -m68060 -ffast-math -fno-strict-aliasing -fcommon -w -U__linux__ -Ulinux \
	-D__amix__ -DSVR4 -fno-builtin-sin -fno-builtin-cos -fno-builtin-tan -fno-builtin-fmod \
	-fno-builtin-sinf -fno-builtin-cosf -Iassets -I$(GATE)/zzgl -I$(GATE)/src/mesa-18.3.6/include -I$(GATE)/compat/amix -I$(GATE)/cores/h264bench -I$(X)/include -I$(M)/include

# libmikmod for the 68060, without its own Makefile's objects in the source
$(BUILD_DIR)/libmikmod.a:
	mkdir -p $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/mm; cp -R $(M) $(BUILD_DIR)/mm
	$(MAKE) -C $(BUILD_DIR)/mm CC=$(CC) AR=$(AMIX_TOOLS)/m68k-elf-ar \
		CFLAGS='-O2 -m68060 -fcommon -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -D__amix__ -DSVR4 -DHAVE_CONFIG_H -Dunix -I. -Iinclude'
	cp $(BUILD_DIR)/mm/libmikmod.a $@

build: $(BUILD_DIR)/libmikmod.a
	$(CC) $(CFLAGS) -c lamerintro.c -o $(BUILD_DIR)/lamerintro.o
	$(CC) $(CFLAGS) -c $(GATE)/zzgl/zzgl_enc.c -o $(BUILD_DIR)/zzgl_enc.o
	$(CC) $(CFLAGS) -c $(GATE)/zzgl/zzgl_amix.c -o $(BUILD_DIR)/zzgl_amix.o
	$(CC) -m68060 -Wl,--allow-multiple-definition -o $(BUILD_DIR)/lamerintro $(BUILD_DIR)/lamerintro.o $(BUILD_DIR)/zzgl_enc.o \
		$(BUILD_DIR)/zzgl_amix.o -L$(BUILD_DIR) -lmikmod -L$(X)/lib -lX11 -lsocket -lnsl -L$(L060) -lm060 -lc060 -lm

.PHONY: build
