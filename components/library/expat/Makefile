# Expat 2.7.3 for AMIX: XML parser (libexpat.so.1, shared, with its headers
# and expat.pc); libdom's XML binding uses it, for SVG in NetSurf.
COMPONENT_NAME    = expat
COMPONENT_VERSION = 2.7.3
COMPONENT_ARCHIVE = expat-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://github.com/libexpat/libexpat/releases/download/R_2_7_3/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 71df8f40706a7bb0a80a5367079ea75d91da4f8c65c58ec59bcdfbf7decdab9f
CLEAN_FILES       = expat-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = expat-$(COMPONENT_VERSION)
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4
# AMIX's math.h declares isnan only when __STDC__ is 0: gcc's builtin;
# -fvisibility=default (after configure's hidden): a static build hides the
# API too, and the shared library is made from it
AMIX_CFLAGS = -O2 -fPIC -fvisibility=default -Disnan=__builtin_isnan

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xJf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	cd build && CC="$(CC_AMIX)" CFLAGS="$(AMIX_CFLAGS)" \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) \
		--disable-shared --enable-static --without-docbook --without-examples --without-tests \
		--without-xmlwf > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
	$(MAKE) -C build -j8 > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# libtool makes no shared library for this target: from the -fPIC archive
	../../../tools/amix-mkshlib build/lib/.libs/libexpat.a libexpat.so.1 build

stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C build/lib install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	$(MAKE) -C build install-pkgconfigDATA DESTDIR=$(STAGE_DIR) >> build/install.log 2>&1
	rm -rf $(STAGE_DIR)$(PREFIX)/lib/*.la $(STAGE_DIR)$(PREFIX)/lib/*.a $(STAGE_DIR)$(PREFIX)/lib/cmake $(STAGE_DIR)$(PREFIX)/share
	cp -P build/libexpat.so.1 build/libexpat.so $(STAGE_DIR)$(PREFIX)/lib/
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/expat-amix.tar usr

.PHONY: build stage
