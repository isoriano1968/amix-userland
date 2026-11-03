# OpenSSL 3.5 LTS for AMIX: libcrypto.so.3 and libssl.so.3 in /usr/local/lib, with
# headers and .pc files.  No assembler for m68k: all C.
COMPONENT_NAME    = openssl
COMPONENT_VERSION = 3.5.4
COMPONENT_ARCHIVE = openssl-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/openssl/openssl/releases/download/openssl-$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99
CLEAN_FILES       = openssl-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = openssl-$(COMPONENT_VERSION)

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -d $(S) -p1 < $$p || exit 1; done
	mkdir -p build
	sh files/configure-amix.sh $(CURDIR)/$(S) > build/configure.log 2>&1
	$(MAKE) -C $(S) -j8 build_libs > build/make.log 2>&1
	m68k-elf-gcc -O2 -m68060 -fPIC -w -c files/amix-ssl.c -o build/amix-ssl.o
	../../../tools/amix-got-check build/amix-ssl.o `find $(S) -name 'libcrypto-lib-*.o' -o -name 'libssl-lib-*.o' | grep -v '/test/'`
	m68k-elf-gcc -shared -Wl,-h,libcrypto.so.3 -Wl,--whole-archive $(S)/libcrypto.a -Wl,--no-whole-archive \
		build/amix-ssl.o -lamixc99 -o build/libcrypto.so.3
	m68k-elf-gcc -shared -Wl,-h,libssl.so.3 -Wl,--whole-archive $(S)/libssl.a -Wl,--no-whole-archive \
		-Lbuild -lcrypto.so.3 -o build/libssl.so.3 2>/dev/null || \
	m68k-elf-gcc -shared -Wl,-h,libssl.so.3 -Wl,--whole-archive $(S)/libssl.a -Wl,--no-whole-archive \
		build/libcrypto.so.3 -o build/libssl.so.3
	m68k-elf-strip build/libcrypto.so.3 build/libssl.so.3
	../../../tools/amix-fix-data-relocs build/libcrypto.so.3
	../../../tools/amix-fix-data-relocs build/libssl.so.3

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp build/libcrypto.so.3 build/libssl.so.3 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libcrypto.so.3 $(STAGE_DIR)$(PREFIX)/lib/libcrypto.so
	ln -sf libssl.so.3 $(STAGE_DIR)$(PREFIX)/lib/libssl.so
	cp -R $(S)/include/openssl $(STAGE_DIR)$(PREFIX)/include/
	rm -f $(STAGE_DIR)$(PREFIX)/include/openssl/*.in
	for p in libcrypto libssl openssl; do sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' $$p.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/$$p.pc; done

.PHONY: build stage
