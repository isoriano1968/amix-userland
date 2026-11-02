# GLU 9.0.3 for AMIX: the SGI GLU, a shared library with its header and glu.pc.
COMPONENT_NAME    = glu
COMPONENT_VERSION = 9.0.3
COMPONENT_ARCHIVE = glu-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://archive.mesa3d.org/glu/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = bd43fe12f374b1192eb15fe20e45ff456b9bc26ab57f0eee919f96ca0f8a330f
CLEAN_FILES       = glu-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = glu-$(COMPONENT_VERSION)
L = $(AMIX_SYSROOT)/usr/local/lib
FLAGS = -O2 -m68060 -fPIC -w -U__linux__ -Ulinux -D__amix__ -DSVR4 -DNDEBUG -DLIBRARYBUILD \
	-I$(S)/include -I$(S)/src/include -I$(S)/src/libnurbs/internals -I$(S)/src/libnurbs/interface \
	-I$(S)/src/libnurbs/nurbtess -I$(S)/src/libtess -I$(S)/src/libutil \
	-I$(AMIX_SYSROOT)/usr/local/include -I$(AMIX_SYSROOT)/usr/x11r6/include

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xJf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -p1 < $$p || exit 1; done
	mkdir -p build
	for f in `grep -oE "lib[a-z]+/[^']+[.]c'" $(S)/src/meson.build | tr -d "'"`; do \
		m68k-elf-gcc $(FLAGS) -c $(S)/src/$$f -o build/`basename $$f .c`.o || exit 1; done
	for f in `grep -oE "lib[a-z]+/[^']+[.]cc'" $(S)/src/meson.build | tr -d "'"`; do \
		m68k-elf-g++ $(FLAGS) -fno-exceptions -fno-rtti -c $(S)/src/$$f -o build/`basename $$f .cc`.o || exit 1; done
	../../../tools/amix-got-check build/*.o
	m68k-elf-gcc -shared -Wl,-h,libGLU.so.1 build/*.o -L$(L) -lGL -lsupc++ -lm \
		-Wl,-rpath-link,$(L) -Wl,-rpath-link,$(AMIX_SYSROOT)/usr/x11r6/lib -o build/libGLU.so.1
	m68k-elf-strip build/libGLU.so.1
	../../../tools/amix-fix-data-relocs build/libGLU.so.1

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include/GL
	cp build/libGLU.so.1 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libGLU.so.1 $(STAGE_DIR)$(PREFIX)/lib/libGLU.so
	cp $(S)/include/GL/glu.h $(STAGE_DIR)$(PREFIX)/include/GL/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' glu.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/glu.pc

.PHONY: build stage
