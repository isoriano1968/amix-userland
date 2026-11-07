# SDL2_image 2.8 for AMIX: libSDL2_image-2.0.so.0 with BMP, GIF, ILBM/LBM, PCX,
# PNG and JPEG (the system libpng and libjpeg), PNM, TGA, XPM, QOI; saving PNG
# and JPEG.  No AVIF, JXL, SVG, TIFF, WebP.
# On the shared libSDL2 of library/sdl2; exports IMG's API (DECLSPEC) only.
COMPONENT_NAME    = sdl2-image
COMPONENT_VERSION = 2.8.12
COMPONENT_ARCHIVE = SDL2_image-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/libsdl-org/SDL_image/releases/download/release-$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 393f5efb50536ec13ca4f4affb69cc9966d3c3f969e6c5e701faddf9f9785381
CLEAN_FILES       = SDL2_image-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S     = SDL2_image-$(COMPONENT_VERSION)
PKGS  = sdl2 libpng libjpeg zlib
FMTS  = BMP GIF LBM PCX PNG JPG PNM TGA XPM QOI
SRCS  = IMG IMG_bmp IMG_gif IMG_lbm IMG_pcx IMG_png IMG_jpg IMG_pnm IMG_tga IMG_xpm IMG_qoi \
	IMG_stb IMG_avif IMG_jxl IMG_svg IMG_tif IMG_webp IMG_xcf IMG_xv IMG_xxx
CFLAGS = -O2 -m68060 -fPIC -fvisibility=hidden -fno-strict-aliasing -U__linux__ -Ulinux -D__amix__ -DSVR4 \
	-I$(S)/include $(foreach f,$(FMTS),-DLOAD_$(f)) -DSDL_IMAGE_SAVE_PNG=1 -DSDL_IMAGE_SAVE_JPG=1

SHLIBS                               = libSDL2_image-2.0.so.0
SHLIB_ARCHIVE_libSDL2_image-2.0.so.0 = build/libSDL2_image.a
# --as-needed: sdl2.pc also names X, Pth and libsocket for programs; the
# library itself needs only SDL, libpng, libjpeg and zlib
SHLIB_ARGS_libSDL2_image-2.0.so.0    = -Wl,--as-needed $(shell $(PKG_CONFIG) --libs $(PKGS))

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	mkdir -p build
	for f in $(SRCS); do m68k-elf-gcc $(CFLAGS) $$($(PKG_CONFIG) --cflags $(PKGS)) \
		-c $(S)/src/$$f.c -o build/$$f.o || exit 1; done
	m68k-elf-ar rc build/libSDL2_image.a build/*.o
	$(mkshlibs)

# SDL2_image.pc from upstream's template.  As upstream, libSDL2_image.so ->
# libSDL2_image-2.0.so -> libSDL2_image-2.0.so.0, so that -lSDL2_image finds
# it; on AMIX amix-stubs puts the link stub in libSDL2_image-2.0.so.
stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include/SDL2
	cp $(S)/include/SDL_image.h $(STAGE_DIR)$(PREFIX)/include/SDL2/
	sed -e 's|@prefix@|$(PREFIX)|; s|@exec_prefix@|$${prefix}|; s|@libdir@|$${exec_prefix}/lib|' \
		-e 's|@includedir@|$${prefix}/include|; s|@PACKAGE@|SDL2_image|; s|@VERSION@|$(COMPONENT_VERSION)|' \
		-e 's|@SDL_VERSION@|2.0.9|; s|@PC_REQUIRES@|libpng libjpeg zlib|; s|@PC_LIBS@||' \
		$(S)/SDL2_image.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/SDL2_image.pc
	ln -s libSDL2_image-2.0.so $(STAGE_DIR)$(PREFIX)/lib/libSDL2_image.so
	$(stage_finish)
	@$(manifest_files)

.PHONY: build stage
