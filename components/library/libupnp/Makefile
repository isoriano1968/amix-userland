# libupnp (pupnp) 22.1.10 for AMIX: the UPnP control point (SSDP search, the
# description, SOAP actions such as ContentDirectory Browse) for programs that
# find DLNA servers; libupnp.so.22 and libixml.so.22, with headers and .pc
# files.  No device side, no IPv6.  Its threads are GNU Pth's (library/pth):
# every file is compiled with Pth's pthread.h, so its waits yield.  AMIX has
# no multicast: the searches go out as broadcasts (patches/0001).
COMPONENT_NAME    = libupnp
COMPONENT_VERSION = 22.1.10
COMPONENT_ARCHIVE = libupnp-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://github.com/pupnp/pupnp/releases/download/release-$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 33b1dcb23a824a348ef4553f38b22a37f4d17e27d0998b3f33c156b3dcd8e06b
CLEAN_FILES       = libupnp-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S   = libupnp-$(COMPONENT_VERSION)
R   = $(AMIX_SYSROOT)
G   = /usr/local/gnu
T   = ../../../tools
# files/amix-upnp.h: the socket prototypes, address types and the rest SVR4
# lacks; files/include: sched.h and ifaddrs.h
CF  = -O2 -fPIC -fno-split-wide-types -I$(CURDIR)/files/include \
	$(shell $(PKG_CONFIG) --variable=pth_cflags pth) -include $(CURDIR)/files/amix-upnp.h
CMAKE_ARGS = -DCMAKE_TOOLCHAIN_FILE=$(abspath ../../../make-rules/amix-toolchain.cmake) \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_C_FLAGS="$(CF)" \
	-DCMAKE_EXE_LINKER_FLAGS="-L$(R)$(G)/lib -Wl,-rpath-link,$(R)$(G)/lib" \
	-DCMAKE_REQUIRED_LIBRARIES="pthread;pth;socket;nsl" \
	-DCMAKE_THREAD_LIBS_INIT="-lpthread -lpth" -DCMAKE_USE_PTHREADS_INIT=1 -DTHREADS_PREFER_PTHREAD_FLAG=OFF \
	-DUPNP_BUILD_SHARED=ON -DUPNP_BUILD_STATIC=OFF -DUPNP_ENABLE_DEVICE_API=OFF -DUPNP_ENABLE_WEBSERVER=OFF \
	-DUPNP_ENABLE_IPV6=OFF -DUPNP_BUILD_SAMPLES=OFF -DUPNP_ENABLE_TESTING=OFF \
	-DUPNP_ENABLE_TESTING_INTEGRATION=OFF -DUPNP_ENABLE_BACKTRACE=OFF -DUPNP_ENABLE_BLOCKING_TCP_CONNECTIONS=ON

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xjf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build/cmake
	cd build/cmake && cmake ../../$(S) $(CMAKE_ARGS) > ../cmake.log 2>&1 || { tail -20 ../cmake.log; exit 1; }
	$(MAKE) -C build/cmake -j8 > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	# the shared targets' objects (internals hidden, the API exported, as
	# upstream's shared libraries), linked for AMIX by amix-mkshlib
	m68k-elf-gcc -m68060 $(CF) -c files/amix-upnp.c -o build/amix-upnp.o
	rm -f build/libixml-pic.a build/libupnp-pic.a
	m68k-elf-ar rc build/libixml-pic.a `find build/cmake/ixml/CMakeFiles/ixml_shared.dir -name '*.obj'`
	m68k-elf-ar rc build/libupnp-pic.a `find build/cmake/upnp/CMakeFiles/upnp_shared.dir -name '*.obj'` build/amix-upnp.o
	$(T)/amix-mkshlib build/libixml-pic.a libixml.so.22 build -L$(R)/usr/local/lib -lamixc99
	$(T)/amix-mkshlib build/libupnp-pic.a libupnp.so.22 build -Lbuild -lixml \
		-L$(R)$(G)/lib -lpthread -lpth -lsocket -lnsl -Wl,-R,$(PREFIX)/lib:$(G)/lib -lamixc99

stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C build/cmake install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	rm -rf $(STAGE_DIR)$(PREFIX)/lib/*.a $(STAGE_DIR)$(PREFIX)/lib/lib*.so* $(STAGE_DIR)$(PREFIX)/lib/cmake $(STAGE_DIR)$(PREFIX)/share
	cp -P build/libupnp.so.22 build/libupnp.so build/libixml.so.22 build/libixml.so $(STAGE_DIR)$(PREFIX)/lib/
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/libupnp-amix.tar usr

.PHONY: build stage
