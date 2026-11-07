# SDL 2 for AMIX: the shared libSDL2-2.0.so.0 with Paula audio, pth threads and the zz9k renderer.
COMPONENT_NAME    = sdl2
COMPONENT_VERSION = 2.30.12
COMPONENT_ARCHIVE = SDL2-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/libsdl-org/SDL/releases/download/release-$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea

CLEAN_FILES       = SDL2-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

S = build/SDL2-$(COMPONENT_VERSION)

# the library names X (with XShm), gnu-pth, libsocket and libnsl itself, with
# their runpaths; MIT-SHM's shm* calls are its own (amix/src/core/amix).
# What it takes from the archives x11.pc names (libamixc99's vsnprintf, libm)
# stays inside: exported, it would stand in for libpth's and the program's.
SHLIBS                         = libSDL2-2.0.so.0
SHLIB_ARCHIVE_libSDL2-2.0.so.0 = build/libSDL2.a
SHLIB_ARGS_libSDL2-2.0.so.0    = $(shell $(PKG_CONFIG) --libs xext x11 pth) -lsocket -lnsl \
	-Wl,--exclude-libs,libamixc99.a:libgen.a:libm.a

build:
	sh fetch.sh
	sh build.sh
	$(mkshlibs)

# headers, sdl2.pc, sdl2-config, sdl2.m4 and the library; no libSDL2.a.
# As upstream: libSDL2.so -> libSDL2-2.0.so -> libSDL2-2.0.so.0, so that
# -lSDL2 finds it; on AMIX amix-stubs puts the link stub in libSDL2-2.0.so.
stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/include/SDL2 $(STAGE_DIR)$(PREFIX)/lib/pkgconfig \
		$(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/share/aclocal
	cp $(S)/include/*.h $(STAGE_DIR)$(PREFIX)/include/SDL2/
	cp $(S)/sdl2.pc $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/
	cp $(S)/sdl2-config $(STAGE_DIR)$(PREFIX)/bin/
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/sdl2-config
	cp $(S)/sdl2.m4 $(STAGE_DIR)$(PREFIX)/share/aclocal/
	ln -s libSDL2-2.0.so $(STAGE_DIR)$(PREFIX)/lib/libSDL2.so
	$(stage_finish)
	@$(manifest_files)

.PHONY: build stage
