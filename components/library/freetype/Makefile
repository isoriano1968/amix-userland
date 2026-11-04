# FreeType 2.14.1 for AMIX: font rendering (TrueType, OpenType, Type 1, ...);
# libfreetype.so.6 (shared), with its headers and freetype2.pc.  zlib for compressed fonts;
# no HarfBuzz, Brotli, bzip2 or PNG (colour emoji).
COMPONENT_NAME    = freetype
COMPONENT_VERSION = 2.14.1
COMPONENT_ARCHIVE = freetype-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://download.savannah.gnu.org/releases/freetype/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc
CLEAN_FILES       = freetype-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = freetype-$(COMPONENT_VERSION)
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xJf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fPIC" CC_BUILD=cc PKG_CONFIG=$(PKG_CONFIG) \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) \
		--disable-shared --enable-static --with-zlib=yes --with-bzip2=no --with-png=no \
		--with-harfbuzz=no --with-brotli=no --with-librsvg=no > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
	$(MAKE) -C build -j8 > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# libtool makes no shared library for this target: from the -fPIC archive
	../../../tools/amix-mkshlib build/.libs/libfreetype.a libfreetype.so.6 build \
		-L$(AMIX_SYSROOT)$(PREFIX)/lib -lz -Wl,-R,$(PREFIX)/lib

stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C build install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	rm -rf $(STAGE_DIR)$(PREFIX)/lib/*.la $(STAGE_DIR)$(PREFIX)/lib/*.a $(STAGE_DIR)$(PREFIX)/share
	cp -P build/libfreetype.so.6 build/libfreetype.so $(STAGE_DIR)$(PREFIX)/lib/
	chmod -R go-w $(STAGE_DIR)
	$(stage_stubs)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/freetype-amix.tar usr

.PHONY: build stage
