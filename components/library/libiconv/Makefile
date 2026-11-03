# GNU libiconv 1.18 for AMIX: libiconv.so.2 and libcharset.so.1 (shared, with
# iconv.h, localcharset.h and libiconv.pc); AMIX's libc has no iconv.  The iconv program is left out,
# so nothing clashes with AMIX's own tools.
COMPONENT_NAME    = libiconv
COMPONENT_VERSION = 1.18
COMPONENT_ARCHIVE = libiconv-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftpmirror.gnu.org/libiconv/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
CLEAN_FILES       = libiconv-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = libiconv-$(COMPONENT_VERSION)
R  = $(AMIX_SYSROOT)
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -std=gnu99 -U__linux__ -Ulinux -D__amix__ -DSVR4

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fPIC -fno-split-wide-types" CPPFLAGS="-I$(R)/usr/local/include" \
		LDFLAGS="-L$(R)/usr/local/lib" \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) \
		--disable-shared --enable-static --disable-nls > configure.log 2>&1
	# srclib (gnulib) is for the iconv program only
	$(MAKE) -C build lib/localcharset.h > build/make.log 2>&1 && $(MAKE) -C build/lib -j8 >> build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# libtool makes no shared library for this target: from the -fPIC archives
	../../../tools/amix-mkshlib build/lib/.libs/libiconv.a libiconv.so.2 build
	../../../tools/amix-mkshlib build/libcharset/lib/.libs/libcharset.a libcharset.so.1 build

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp -P build/libiconv.so.2 build/libiconv.so build/libcharset.so.1 build/libcharset.so $(STAGE_DIR)$(PREFIX)/lib/
	cp build/include/iconv.h build/lib/localcharset.h $(STAGE_DIR)$(PREFIX)/include/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' libiconv.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libiconv.pc
	chmod 755 $(STAGE_DIR)$(PREFIX)/lib/*.so.*; chmod 644 $(STAGE_DIR)$(PREFIX)/include/*.h $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/*.pc
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/libiconv-amix.tar usr

.PHONY: build stage
