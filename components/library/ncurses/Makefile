# ncurses 6.6 for AMIX: shared libraries, narrow characters, AMIX's terminfo first.
COMPONENT_NAME    = ncurses
COMPONENT_VERSION = 6.6
COMPONENT_ARCHIVE = ncurses-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftp.gnu.org/gnu/ncurses/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11
CLEAN_FILES       = ncurses-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build: $(COMPONENT_ARCHIVE)
	tar xzf $(COMPONENT_ARCHIVE)
	sh configure-amix.sh
	$(MAKE) -C build
	../../../tools/amix-got-check `find build -name "*.o"`
	cd build/lib && \
	m68k-elf-gcc -shared -Wl,-h,libncurses.so.6 -Wl,--whole-archive libncurses.a -Wl,--no-whole-archive -o libncurses.so.6 && \
	ln -sf libncurses.so.6 libncurses.so && \
	for l in form menu panel; do \
		m68k-elf-gcc -shared -Wl,-h,lib$$l.so.6 -Wl,--whole-archive lib$$l.a -Wl,--no-whole-archive -L. -lncurses -o lib$$l.so.6 && \
		ln -sf lib$$l.so.6 lib$$l.so; \
	done
	rm -f build/progs/clear build/progs/infocmp build/progs/tabs build/progs/tic build/progs/toe build/progs/tput build/progs/tset
	$(MAKE) -C build/progs LDFLAGS=-Wl,-R,$(PREFIX)/lib

# everything where it belongs on AMIX, shared libraries only
stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C build install.progs install.includes DESTDIR=$(STAGE_DIR) INSTALL_PROG='install -c'
	m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/n*
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	cp -P build/lib/*.so build/lib/*.so.6 $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf libncurses.so.6 $(STAGE_DIR)$(PREFIX)/lib/libcurses.so
	for l in ncurses form menu panel; do \
		sed -e 's|@NAME@|'$$l'|' -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' ncurses.pc.in \
			> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/$$l.pc; \
	done
	$(stage_stubs)

.PHONY: build stage
