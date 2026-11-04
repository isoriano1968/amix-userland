# IJG libjpeg 9f for AMIX: libjpeg.so.9 (shared, with its headers and libjpeg.pc).
COMPONENT_NAME    = libjpeg
COMPONENT_VERSION = 9f
COMPONENT_ARCHIVE = jpegsrc.v$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://www.ijg.org/files/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b
CLEAN_FILES       = jpeg-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = jpeg-$(COMPONENT_VERSION)
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -std=gnu99 -U__linux__ -Ulinux -D__amix__ -DSVR4

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fPIC" \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) \
		--disable-shared --enable-static > configure.log 2>&1
	$(MAKE) -C build -j8 libjpeg.la > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# libtool makes no shared library for this target: from the -fPIC archive
	../../../tools/amix-mkshlib build/.libs/libjpeg.a libjpeg.so.9 build

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp -P build/libjpeg.so.9 build/libjpeg.so $(STAGE_DIR)$(PREFIX)/lib/
	cp build/jconfig.h $(S)/jpeglib.h $(S)/jmorecfg.h $(S)/jerror.h $(STAGE_DIR)$(PREFIX)/include/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' libjpeg.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libjpeg.pc
	chmod 755 $(STAGE_DIR)$(PREFIX)/lib/*.so.*; chmod 644 $(STAGE_DIR)$(PREFIX)/include/*.h $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/*.pc
	$(stage_stubs)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/libjpeg-amix.tar usr

.PHONY: build stage
