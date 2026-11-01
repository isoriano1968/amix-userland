# zlib 1.3.1 for AMIX: a shared library, its headers and zlib.pc.
COMPONENT_NAME    = zlib
COMPONENT_VERSION = 1.3.1
COMPONENT_ARCHIVE = zlib-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://zlib.net/fossils/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
CLEAN_FILES       = zlib-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = zlib-$(COMPONENT_VERSION)
SRCS = adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate \
	inftrees trees uncompr zutil
CFLAGS = -O2 -m68060 -fPIC -w -U__linux__ -Ulinux -D__amix__ -DSVR4 -DHAVE_UNISTD_H -DHAVE_STDARG_H

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	mkdir -p build
	for f in $(SRCS); do m68k-elf-gcc $(CFLAGS) -c $(S)/$$f.c -o build/$$f.o || exit 1; done
	../../../tools/amix-got-check build/*.o
	m68k-elf-gcc -shared -Wl,-h,libz.so.1 build/*.o -o build/libz.so.1
	m68k-elf-strip build/libz.so.1
	../../../tools/amix-fix-data-relocs build/libz.so.1

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp build/libz.so.1 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libz.so.1 $(STAGE_DIR)$(PREFIX)/lib/libz.so
	cp $(S)/zlib.h $(S)/zconf.h $(STAGE_DIR)$(PREFIX)/include/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' zlib.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/zlib.pc

.PHONY: build stage
