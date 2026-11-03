# GNU Pth for AMIX: cooperative threads and its pthread library, shared
# (libpth.so.20, libpthread.so.20) with pth.h, pthread.h and pth.pc in
# /usr/local/gnu, apart from /usr/local so that other builds do not pick up
# its pthread.h by accident.  A program or library that uses it compiles
# every file with -include pthread.h (pth.pc's pth_cflags): that maps read(),
# select(), sleep() and the rest to Pth's yielding versions.
COMPONENT_NAME    = pth
COMPONENT_VERSION = 2.0.7
COMPONENT_ARCHIVE = pth-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftpmirror.gnu.org/pth/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 72353660c5a2caafd601b20e12e75d865fd88f6cf1a088b306a3963f0bc77232
CLEAN_FILES       = pth-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)
PREFIX            = /usr/local/gnu

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = pth-$(COMPONENT_VERSION)
# thread switches without system calls: _setjmp/_longjmp, sigaltstack only
# to start a thread (the default, getcontext/setcontext, traps every switch)
PTH_MCTX = --with-mctx-mth=sjlj --with-mctx-dsp=usjlj --with-mctx-stk=sas

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	mkdir -p build
	cd $(S) && CC=m68k-elf-gcc AR=m68k-elf-ar RANLIB=m68k-elf-ranlib \
		CFLAGS='-O2 -m68060 -std=gnu89 -fno-strict-aliasing -fPIC' \
		./configure --host=m68k-cbm-sysv4 --build=x86_64-linux-gnu --prefix=$(PREFIX) \
		--enable-pthread --disable-shared --enable-static $(PTH_MCTX) > ../build/configure.log 2>&1
	# after configure: 0002 changes the generated pthread.h
	for p in patches/*.patch; do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	$(MAKE) -C $(S) pth_p.h > build/make.log 2>&1
	$(MAKE) -C $(S) >> build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# libtool makes no shared library for this target: from the -fPIC archives
	../../../tools/amix-mkshlib $(S)/.libs/libpth.a libpth.so.20 build -lsocket -lnsl -lamixc99
	# Pth's libpthread.a is pthread.o plus all of Pth again: the shared one is
	# pthread.o alone on libpth.so.20, one scheduler in a process
	cd build && m68k-elf-ar x ../$(S)/.libs/libpthread.a pthread.o && m68k-elf-ar rc libpthread-only.a pthread.o && rm pthread.o
	../../../tools/amix-mkshlib build/libpthread-only.a libpthread.so.20 build \
		-Lbuild -lpth -Wl,-R,$(PREFIX)/lib

stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C $(S) install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	rm -rf $(STAGE_DIR)$(PREFIX)/lib/*.a $(STAGE_DIR)$(PREFIX)/lib/*.la $(STAGE_DIR)$(PREFIX)/share/man
	cp -P build/libpth.so.20 build/libpth.so build/libpthread.so.20 build/libpthread.so $(STAGE_DIR)$(PREFIX)/lib/
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' files/pth.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/pth.pc
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/pth-amix.tar usr

.PHONY: build stage
