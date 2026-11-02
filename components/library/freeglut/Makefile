# freeglut 3.4.0 for AMIX: GLUT as a shared library, with headers and glut.pc.
COMPONENT_NAME    = freeglut
COMPONENT_VERSION = 3.4.0
COMPONENT_ARCHIVE = freeglut-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/freeglut/freeglut/releases/download/v$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 3c0bcb915d9b180a97edaebd011b7a1de54583a838644dcd42bb0ea0c6f3eaec
CLEAN_FILES       = freeglut-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S = freeglut-$(COMPONENT_VERSION)
L = $(AMIX_SYSROOT)/usr/local/lib
XL = $(AMIX_SYSROOT)/usr/x11r6/lib
# no XInput2, Xrandr, XF86VidMode or joystick on R6.3: files/config.h says so
FLAGS = -O2 -m68060 -fPIC -w -std=gnu99 -U__linux__ -Ulinux -D__amix__ -DSVR4 -D__unix__ -DHAVE_CONFIG_H -DFREEGLUT_EXPORTS \
	-include sys/time.h -Ifiles -I$(S)/include -I$(S)/src -I$(AMIX_SYSROOT)/usr/local/include \
	-I$(AMIX_SYSROOT)/usr/x11r6/include
SRCS = fg_callbacks fg_cursor fg_display fg_ext fg_font fg_font_data fg_gamemode fg_geometry fg_gl2 \
	fg_init fg_input_devices fg_joystick fg_main fg_misc fg_overlay fg_spaceball fg_state \
	fg_stroke_mono_roman fg_stroke_roman fg_structure fg_teapot fg_videoresize fg_window fg_menu \
	x11/fg_cursor_x11 x11/fg_display_x11_glx x11/fg_ext_x11 x11/fg_gamemode_x11 x11/fg_glutfont_definitions_x11 \
	x11/fg_init_x11 x11/fg_input_devices_x11 x11/fg_joystick_x11 x11/fg_main_x11 x11/fg_menu_x11 \
	x11/fg_spaceball_x11 x11/fg_state_x11 x11/fg_state_x11_glx x11/fg_structure_x11 x11/fg_window_x11 \
	x11/fg_window_x11_glx x11/fg_xinput_x11 x11/fg_cmap_x11

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -p1 < $$p || exit 1; done
	mkdir -p build
	for f in $(SRCS); do \
		m68k-elf-gcc $(FLAGS) -c $(S)/src/$$f.c -o build/`basename $$f`.o || exit 1; done
	../../../tools/amix-got-check build/*.o
	m68k-elf-gcc -shared -Wl,-h,libglut.so.3 build/*.o -L$(L) -lGL -L$(XL) -lXext -lX11 -lsocket -lnsl -lm \
		-Wl,-rpath-link,$(L) -Wl,-rpath-link,$(XL) -Wl,-R,/usr/local/lib:/usr/x11r6/lib -o build/libglut.so.3
	m68k-elf-strip build/libglut.so.3
	../../../tools/amix-fix-data-relocs build/libglut.so.3

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include/GL
	cp build/libglut.so.3 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libglut.so.3 $(STAGE_DIR)$(PREFIX)/lib/libglut.so
	cp $(S)/include/GL/freeglut.h $(S)/include/GL/freeglut_ext.h $(S)/include/GL/freeglut_std.h $(S)/include/GL/freeglut_ucall.h $(S)/include/GL/glut.h $(STAGE_DIR)$(PREFIX)/include/GL/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' glut.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/glut.pc

.PHONY: build stage
