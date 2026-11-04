# libpng 1.6.44 for AMIX: a shared library on zlib, its headers and libpng.pc.
# make -C ../zlib build sysroot-install first.
COMPONENT_NAME    = libpng
COMPONENT_VERSION = 1.6.44
COMPONENT_ARCHIVE = libpng-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://download.sourceforge.net/libpng/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 60c4da1d5b7f0aa8d158da48e8f8afa9773c1c8baa5d21974df61f1886b8ce8e
CLEAN_FILES       = libpng-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = libpng-$(COMPONENT_VERSION)
SRCS = png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans \
	pngwio pngwrite pngwtran pngwutil
CFLAGS = -O2 -m68060 -fPIC -w -U__linux__ -Ulinux -D__amix__ -DSVR4 -DPNG_ARM_NEON_OPT=0 \
	-I$(S) -Ibuild -I$(AMIX_SYSROOT)$(PREFIX)/include

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xJf $(COMPONENT_ARCHIVE)
	mkdir -p build
	cp $(S)/scripts/pnglibconf.h.prebuilt build/pnglibconf.h
	cat gcc27.h >> build/pnglibconf.h
	for f in $(SRCS); do m68k-elf-gcc $(CFLAGS) -c $(S)/$$f.c -o build/$$f.o || exit 1; done
	../../../tools/amix-got-check build/*.o
	m68k-elf-gcc -shared -Wl,-h,libpng16.so.16 build/*.o -L$(AMIX_SYSROOT)$(PREFIX)/lib -lz -lm \
		-Wl,-R,$(PREFIX)/lib -o build/libpng16.so.16
	m68k-elf-strip build/libpng16.so.16
	../../../tools/amix-fix-data-relocs build/libpng16.so.16
	../../../tools/amix-fix-needed build/libpng16.so.16

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp build/libpng16.so.16 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libpng16.so.16 $(STAGE_DIR)$(PREFIX)/lib/libpng16.so
	ln -sf libpng16.so.16 $(STAGE_DIR)$(PREFIX)/lib/libpng.so
	cp $(S)/png.h $(S)/pngconf.h build/pnglibconf.h $(STAGE_DIR)$(PREFIX)/include/
	mkdir -p $(STAGE_DIR)$(PREFIX)/include/libpng16
	for h in png.h pngconf.h pnglibconf.h; do ln -sf ../$$h $(STAGE_DIR)$(PREFIX)/include/libpng16/; done
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' libpng.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libpng.pc
	ln -sf libpng.pc $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libpng16.pc

.PHONY: build stage
