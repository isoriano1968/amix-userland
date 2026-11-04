# bzip2 1.0.8 for AMIX: the bzip2 compressor and libbz2.so.1.0 (shared, with
# bzlib.h and bzip2.pc); replaces the static 1.0.2 package of 2005.  A plain
# Makefile upstream: the recipe compiles the files itself and uses
# make-rules/unpack.mk and shlib.mk, the example of a component without a
# build system of its own.
COMPONENT_NAME    = bzip2
COMPONENT_VERSION = 1.0.8
COMPONENT_ARCHIVE = bzip2-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://sourceware.org/pub/bzip2/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
CC     = m68k-elf-gcc -m68060 -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4
CFLAGS = -O2 -fPIC -D_FILE_OFFSET_BITS=64 -Wall -Winline -fno-strength-reduce
LIBOBJ = blocksort huffman crctable randtable compress decompress bzlib

# the library from upstream's object list, its soname as upstream's
# Makefile-libbz2_so gives it
SHLIBS                      = libbz2.so.1.0
SHLIB_ARCHIVE_libbz2.so.1.0 = build/libbz2.a
SHLIB_ARGS_libbz2.so.1.0    = -L$(AMIX_SYSROOT)/usr/local/lib -lamixc99

# the programs link the shared library and find it by their runpath
PROG_LIBS = -Lbuild -lbz2 -Wl,-R,$(PREFIX)/lib -L$(AMIX_SYSROOT)/usr/local/lib -lamixc99

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	for f in $(LIBOBJ) bzip2 bzip2recover; do \
		$(CC) $(CFLAGS) -c $(S)/$$f.c -o build/$$f.o || exit 1; done
	m68k-elf-ar rcs build/libbz2.a $(LIBOBJ:%=build/%.o)
	$(mkshlibs)
	$(CC) -o build/bzip2 build/bzip2.o $(PROG_LIBS)
	$(CC) -o build/bzip2recover build/bzip2recover.o $(PROG_LIBS)
	m68k-elf-strip build/bzip2 build/bzip2recover

# bunzip2 and bzcat are bzip2 under other names, as upstream installs them
stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/include \
		$(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/share/man/man1
	cp build/bzip2 build/bzip2recover $(STAGE_DIR)$(PREFIX)/bin/
	ln -sf bzip2 $(STAGE_DIR)$(PREFIX)/bin/bunzip2
	ln -sf bzip2 $(STAGE_DIR)$(PREFIX)/bin/bzcat
	cp $(S)/bzlib.h $(STAGE_DIR)$(PREFIX)/include/
	cp $(S)/bzip2.1 $(STAGE_DIR)$(PREFIX)/share/man/man1/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' files/bzip2.pc.in \
		> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/bzip2.pc
	$(stage_finish)

.PHONY: build stage
