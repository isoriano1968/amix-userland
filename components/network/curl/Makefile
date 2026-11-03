# curl 8.23 for AMIX: the curl tool and libcurl.so.4 (shared, with headers
# and libcurl.pc), on OpenSSL and zlib; CA bundle /etc/ssl/cert.pem (network/ca-certificates).
COMPONENT_NAME    = curl
COMPONENT_VERSION = 8.23.0
COMPONENT_ARCHIVE = curl-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://curl.se/download/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
CLEAN_FILES       = curl-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = curl-$(COMPONENT_VERSION)
R  = $(AMIX_SYSROOT)
# amix-curl.h: socklen_t and the socket prototypes SVR4's headers lack;
# -fno-split-wide-types: gcc 14's 64-bit subtraction bug (sub.l mem,mem)
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -std=gnu99 -U__linux__ -Ulinux -D__amix__ -DSVR4 \
	-include $(CURDIR)/files/amix-curl.h

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fPIC -fno-split-wide-types" CPPFLAGS="-I$(R)/usr/local/include" \
		LDFLAGS="-L$(R)/usr/local/lib -Wl,-rpath-link,$(R)/usr/local/lib -Wl,-R,/usr/local/lib" \
		LIBS="-lamixc99 -lsocket -lnsl -lgen" \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) \
		--with-openssl=$(R)/usr/local --with-zlib=$(R)/usr/local \
		--without-ca-bundle --with-ca-path=/etc/ssl/certs \
		--disable-ldap --disable-ldaps --without-libpsl --without-brotli --without-zstd \
		--without-nghttp2 --without-libidn2 --without-libssh2 --disable-threaded-resolver \
		--disable-shared --enable-static --disable-symbol-hiding > configure.log 2>&1
	$(MAKE) -C build -j8 > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# libtool makes no shared library for this target: from the -fPIC archive
	# (all symbols visible: a static build hides all but the API, and the
	# tool uses curlx_ too), and the curl tool linked again, to it
	../../../tools/amix-mkshlib build/lib/.libs/libcurl.a libcurl.so.4 build \
		-L$(R)/usr/local/lib -lssl -lcrypto -lz -lsocket -lnsl -lgen -lamixc99 -Wl,-R,/usr/local/lib
	rm -f build/src/curl
	$(MAKE) -C build/src curl curl_DEPENDENCIES= \
		curl_LDADD="$(CURDIR)/build/libcurl.so.4 -lssl -lcrypto -lz -lamixc99 -lsocket -lnsl -lgen" >> build/make.log 2>&1 || { tail -20 build/make.log; exit 1; }

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include/curl \
		$(STAGE_DIR)$(PREFIX)/share/man/man1
	cp build/src/curl $(STAGE_DIR)$(PREFIX)/bin/ && m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/curl
	cp -P build/libcurl.so.4 build/libcurl.so $(STAGE_DIR)$(PREFIX)/lib/
	cp $(S)/include/curl/*.h $(STAGE_DIR)$(PREFIX)/include/curl/
	# the PC's paths out: the box finds everything in $(PREFIX)/lib
	# and libcurl.so names what it needs itself
	sed -e 's|-L$(R)/usr/local/lib ||g' -e 's|^Libs: .*|Libs: -L$${libdir} -lcurl|' -e 's| *-DCURL_STATICLIB||g' \
		build/libcurl.pc > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libcurl.pc
	sed -e 's|$(R)||g' -e 's| -include [^ ]*amix-curl.h||g' -e 's| *-DCURL_STATICLIB||g' build/curl-config > $(STAGE_DIR)$(PREFIX)/bin/curl-config
	cp build/docs/cmdline-opts/curl.1 build/docs/curl-config.1 $(STAGE_DIR)$(PREFIX)/share/man/man1/
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/*
	chmod 755 $(STAGE_DIR)$(PREFIX)/lib/libcurl.so.4
	chmod 644 $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libcurl.pc \
		$(STAGE_DIR)$(PREFIX)/include/curl/*.h $(STAGE_DIR)$(PREFIX)/share/man/man1/*
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/curl-amix.tar usr

.PHONY: build stage
