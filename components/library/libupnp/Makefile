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

include ../../../make-rules/shared.mk

R   = $(AMIX_SYSROOT)
G   = /usr/local/gnu
# files/amix-upnp.h: the socket prototypes, address types and the rest SVR4
# lacks; files/include: sched.h and ifaddrs.h
CMAKE_CFLAGS  = -O2 -fPIC -fno-split-wide-types -I$(CURDIR)/files/include \
	$(shell $(PKG_CONFIG) --variable=pth_cflags pth) -include $(CURDIR)/files/amix-upnp.h
CMAKE_OPTIONS = -DCMAKE_EXE_LINKER_FLAGS="-L$(R)$(G)/lib -Wl,-rpath-link,$(R)$(G)/lib" \
	-DCMAKE_REQUIRED_LIBRARIES="pthread;pth;socket;nsl" \
	-DCMAKE_THREAD_LIBS_INIT="-lpthread -lpth" -DCMAKE_USE_PTHREADS_INIT=1 -DTHREADS_PREFER_PTHREAD_FLAG=OFF \
	-DUPNP_BUILD_SHARED=ON -DUPNP_BUILD_STATIC=OFF -DUPNP_ENABLE_DEVICE_API=OFF -DUPNP_ENABLE_WEBSERVER=OFF \
	-DUPNP_ENABLE_IPV6=OFF -DUPNP_BUILD_SAMPLES=OFF -DUPNP_ENABLE_TESTING=OFF \
	-DUPNP_ENABLE_TESTING_INTEGRATION=OFF -DUPNP_ENABLE_BACKTRACE=OFF -DUPNP_ENABLE_BLOCKING_TCP_CONNECTIONS=ON

# the shared targets' objects (internals hidden, the API exported, as
# upstream's shared libraries) into -fPIC archives, with our amix-upnp.c
define BUILD_POST
	m68k-elf-gcc -m68060 $(CMAKE_CFLAGS) -c files/amix-upnp.c -o build/amix-upnp.o
	rm -f build/libixml-pic.a build/libupnp-pic.a
	m68k-elf-ar rc build/libixml-pic.a `find build/cmake/ixml/CMakeFiles/ixml_shared.dir -name '*.obj'`
	m68k-elf-ar rc build/libupnp-pic.a `find build/cmake/upnp/CMakeFiles/upnp_shared.dir -name '*.obj'` build/amix-upnp.o
endef

SHLIBS                        = libixml.so.22 libupnp.so.22
SHLIB_ARCHIVE_libixml.so.22   = build/libixml-pic.a
SHLIB_ARGS_libixml.so.22      = -L$(R)/usr/local/lib -lamixc99
SHLIB_ARCHIVE_libupnp.so.22   = build/libupnp-pic.a
SHLIB_ARGS_libupnp.so.22      = -Lbuild -lixml -L$(R)$(G)/lib -lpthread -lpth -lsocket -lnsl \
	-Wl,-R,$(PREFIX)/lib:$(G)/lib -lamixc99
# cmake's own .so files are not AMIX's: amix-mkshlib's replace them
STAGE_REMOVE = lib/*.a lib/lib*.so* lib/cmake share

include ../../../make-rules/cmake.mk
