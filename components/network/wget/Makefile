# GNU Wget 1.25 for AMIX, in /usr/local: HTTP, HTTPS and FTP on OpenSSL and
# zlib, with the CA bundle of network/ca-certificates.  Built the way gnu.mk
# builds the GNU tools: its compat layer (../../gnu/compat) for what SVR4 lacks.
COMPONENT_NAME    = wget
COMPONENT_VERSION = 1.25.0
COMPONENT_ARCHIVE = wget-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftp.gnu.org/gnu/wget/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 766e48423e79359ea31e41db9e5c289675947a7fcf2efdcedb726ac9d0da3784
CLEAN_FILES       = wget-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S   = wget-$(COMPONENT_VERSION)
R   = $(AMIX_SYSROOT)
GNU = $(CURDIR)/../../gnu/compat
W   = -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
	-Wno-error=incompatible-pointer-types -Wno-error=implicit-int
CC_BASE = m68k-elf-gcc -m68060 -std=gnu99 -D__amix__ -DSVR4 -w $(W)
# -fno-split-wide-types: gcc 14 m68k makes a sub.l of two memory operands without it
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -std=gnu99 -DO_CLOEXEC=0 -U__linux__ -Ulinux \
	-D__amix__ -DSVR4 -w $(W)

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do [ ! -f $$p ] || patch -s -d $(S) -p1 < $$p; done
	mkdir -p build
	$(CC_BASE) -O2 -DAMIX_PROG=\"wget\" -c $(GNU)/amix-gnu.c -o build/amix-gnu.o
	$(CC_BASE) -O2 -c $(GNU)/amix-getopt.c -o build/amix-getopt.o
	$(CC_BASE) -O2 -c $(GNU)/amix-printf.c -o build/amix-printf.o
	m68k-elf-ar rcs build/libamixgnu.a build/amix-gnu.o build/amix-getopt.o build/amix-printf.o
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fno-split-wide-types" CPPFLAGS="-I$(R)/usr/local/include" \
		LDFLAGS="-L$(R)/usr/local/lib -Wl,-rpath-link,$(R)/usr/local/lib -Wl,-R,/usr/local/lib -Wl,--allow-multiple-definition" \
		LIBS="-L$(CURDIR)/build -lamixgnu -lamixc99 -lsocket -lnsl -lgen" \
		PKG_CONFIG=$(CURDIR)/../../../tools/amix-pkg-config \
		ac_cv_func_getprogname=yes ac_cv_func_strtoll=yes ac_cv_func_strtoull=yes \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) --sysconfdir=/etc \
		--with-ssl=openssl --with-zlib --without-libpsl --disable-nls --disable-iri --without-libidn \
		--without-libuuid --disable-pcre --disable-pcre2 --without-metalink --without-cares \
		--disable-threads --disable-year2038 > configure.log 2>&1
	# the forced header only now: configure takes what cc -E prints for flags
	$(MAKE) -C build -j8 CFLAGS="-O2 -fno-split-wide-types -include $(GNU)/amix-gnu.h" > build/make.log 2>&1 \
		|| { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)/etc
	cp build/src/wget $(STAGE_DIR)$(PREFIX)/bin/ && m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/wget
	# as upstream installs it; the CA bundle comes through OpenSSL's default
	# path, /usr/local/ssl/cert.pem from network/ca-certificates
	cp $(S)/doc/sample.wgetrc $(STAGE_DIR)/etc/wgetrc
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/wget
	chmod 644 $(STAGE_DIR)/etc/wgetrc
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/wget-amix.tar usr etc

.PHONY: build stage
