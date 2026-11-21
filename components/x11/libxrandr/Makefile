# libXrandr 1.1.0.2 (X11R7.0) for AMIX: the RandR 1.1 client library,
# libXrandr.so.2, with Xrandr.h and xrandr.pc (ours: R6.3 has no xproto.pc).
# Built against the R6.3 libX11 and libXext; extutil.h (as <X11/extensions/
# extutil.h>) from the R6.3 tree, as x11/libxp does.  Talks to the Xamix RandR backport (x11/xserver-amix).
COMPONENT_NAME    = libxrandr
COMPONENT_VERSION = 1.1.0.2
COMPONENT_ARCHIVE = libXrandr-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://www.x.org/releases/individual/lib/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = f4e48ee9221147c6d6a022c9890183d446cb8342e71bd2945628ff5b877a63c3
PREFIX            = /usr/x11r6
S                 = libXrandr-$(COMPONENT_VERSION)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk
include ../../../make-rules/xc.mk

export PATH := $(AMIX_TOOLS):$(PATH)
# R6.3's own headers that the x11r6.3 package does not install (make-rules/xc.mk)
XC_MEMBERS = xc/include/extensions/extutil.h
CFLAGS = -O2 -m68060 -fPIC -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
	-U__linux__ -Ulinux -DSVR4 -DAMIX -D__amix__ -DMALLOC_0_RETURNS_NULL \
	-Ibuild -I$(S)/include/X11/extensions `$(PKG_CONFIG) --cflags randrproto xrender xext x11` \
	-I$(X11_SRC)/include/extensions

SHLIBS                        = libXrandr.so.2
SHLIB_ARCHIVE_libXrandr.so.2  = build/libXrandr.a
SHLIB_ARGS_libXrandr.so.2     = `$(PKG_CONFIG) --libs xrender xext x11`

build: $(COMPONENT_ARCHIVE) $(XC_TGZ)
	$(unpack)
	$(xc_extract)
	: > build/config.h
	mkdir -p build/X11/extensions && ln -sf $(X11_SRC)/include/extensions/extutil.h build/X11/extensions/
	m68k-elf-gcc $(CFLAGS) -c $(S)/src/Xrandr.c -o build/Xrandr.o 2> build/build.log || \
		{ tail -20 build/build.log; exit 1; }
	m68k-elf-ar rcs build/libXrandr.a build/Xrandr.o
	$(mkshlibs)

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/include/X11/extensions $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	cp $(S)/include/X11/extensions/Xrandr.h $(STAGE_DIR)$(PREFIX)/include/X11/extensions/
	sed 's|@VERSION@|$(COMPONENT_VERSION)|' xrandr.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/xrandr.pc
	$(stage_finish)
	$(manifest_files)

.PHONY: build stage
