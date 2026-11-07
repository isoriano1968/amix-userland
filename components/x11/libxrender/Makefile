# libXrender 0.9.0.2 (X11R7.0) for AMIX: the Render extension client library,
# libXrender.so.1, with Xrender.h and xrender.pc (ours: R6.3 has no xproto.pc).
# Built against the R6.3 libX11: this old version needs nothing newer than
# R6.3's Xlibint.h, extutil.h and region.h (from the R6.3 tree, as x11/libxp does).
# The library is made by make-rules/shlib.mk.
COMPONENT_NAME    = libxrender
COMPONENT_VERSION = 0.9.0.2
COMPONENT_ARCHIVE = libXrender-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://www.x.org/releases/individual/lib/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 80b87d676ee15ebb0fa37678645fe9d718cb7c5369dfab0a1befb2590b71a4a7
PREFIX            = /usr/x11r6
S                 = libXrender-$(COMPONENT_VERSION)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
X11_SRC ?= $(HOME)/Sources/usr-amix/private/xzz9000/src/xc
SRCS = AddTrap Color Composite Cursor FillRect FillRects Filter Glyph Picture Poly Trap Tri Xrender
CFLAGS = -O2 -m68060 -fPIC -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
	-U__linux__ -Ulinux -DSVR4 -DAMIX -D__amix__ -DMALLOC_0_RETURNS_NULL \
	-Ibuild -I$(S)/include/X11/extensions `$(PKG_CONFIG) --cflags renderproto x11` \
	-I$(X11_SRC)/include/extensions -I$(X11_SRC)/lib/X11

SHLIBS                         = libXrender.so.1
SHLIB_ARCHIVE_libXrender.so.1  = build/libXrender.a
SHLIB_ARGS_libXrender.so.1     = `$(PKG_CONFIG) --libs x11`

build: $(COMPONENT_ARCHIVE)
	@test -f $(X11_SRC)/include/extensions/extutil.h || { echo "no R6.3 extutil.h: set X11_SRC"; exit 1; }
	$(unpack)
	: > build/config.h
	for f in $(SRCS); do m68k-elf-gcc $(CFLAGS) -c $(S)/src/$$f.c -o build/$$f.o \
		2>> build/build.log || { tail -20 build/build.log; exit 1; }; done
	m68k-elf-ar rcs build/libXrender.a $(addprefix build/,$(addsuffix .o,$(SRCS)))
	$(mkshlibs)

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/include/X11/extensions $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	cp $(S)/include/X11/extensions/Xrender.h $(STAGE_DIR)$(PREFIX)/include/X11/extensions/
	sed 's|@VERSION@|$(COMPONENT_VERSION)|' xrender.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/xrender.pc
	$(stage_finish)
	$(manifest_files)

.PHONY: build stage
