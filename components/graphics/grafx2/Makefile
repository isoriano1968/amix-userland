# Grafx2 2.9 for AMIX: the 256-colour pixel art paint program in the spirit of
# Deluxe Paint, with its own X11 front end (no SDL), on any TrueColor visual.
# grafx2 in /usr/local/bin, its skins, fonts and defaults in /usr/local/share/grafx2,
# a CDE action.  No Lua scripts, TrueType text, TIFF or recoil formats yet.
COMPONENT_NAME    = grafx2
COMPONENT_VERSION = 2.9
COMPONENT_ARCHIVE = grafX2-v$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://gitlab.com/GrafX2/grafX2/-/archive/v$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 343d3c39cf89a60617858e0c7311bd261d90e28b8ae8794ae177edf6fc1b1556
# the MOS 6502 emulator for the C64 program loader: upstream fetches it too
CPU6502_ARCHIVE   = 6502-v0.1.tar.xz
CPU6502_URL       = https://github.com/redcode/6502/releases/download/v0.1/$(CPU6502_ARCHIVE)
CPU6502_SHA256    = bba50f327163d40b1014f5affc8042137f37b200dae50a4f0fe56b5769b3940f
CLEAN_FILES       = $(COMPONENT_ARCHIVE) $(CPU6502_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S     = build/grafX2-v$(COMPONENT_VERSION)
DATA  = $(PREFIX)/share/grafx2
DT    = $(STAGE_DIR)/etc/dt/appconfig
PKGS  = x11 libpng zlib

$(CPU6502_ARCHIVE):
	curl -fL --retry 3 -o $@.part $(CPU6502_URL)
	echo "$(CPU6502_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@

build: $(COMPONENT_ARCHIVE) $(CPU6502_ARCHIVE)
	rm -rf build
	mkdir -p build
	tar -C build -xzf $(COMPONENT_ARCHIVE)
	tar -C build -xJf $(CPU6502_ARCHIVE)
	cd build/6502 && patch -p1 < ../grafX2-v$(COMPONENT_VERSION)/3rdparty/6502-illegal-opcode.patch
	cp build/6502/sources/6502.c build/6502/API/emulation/CPU/6502.h $(S)/src/
	for p in patches/*.patch; do patch -d $(S) -p1 < $$p || exit 1; done
	$(MAKE) -C $(S)/src -f $(CURDIR)/files/Makefile.amix REVISION=v$(COMPONENT_VERSION) \
		CC="m68k-elf-gcc -m68060" CFLAGS="-O2 -std=gnu99 -fno-strict-aliasing -w" \
		EXTRA_CPPFLAGS="-I$(CURDIR)/files/amix -include $(CURDIR)/files/amix/amix-grafx2.h -D__amix__ -DSVR4 -DAMIX_BINDIR=\\\"$(PREFIX)/bin\\\"" \
		XCFLAGS="$$($(PKG_CONFIG) --cflags $(PKGS))" \
		XLIBS="$$($(PKG_CONFIG) --libs $(PKGS))"
	cp $(S)/src/grafx2 build/
	m68k-elf-strip build/grafx2

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(DATA)/skins $(STAGE_DIR)$(DATA)/fonts \
		$(DT)/types/C $(DT)/icons/C $(DT)/appmanager/C/Desktop_Apps
	cp build/grafx2 $(STAGE_DIR)$(PREFIX)/bin/
	cp $(S)/share/grafx2/gfx2def.ini $(S)/share/grafx2/gfx2.gif $(S)/share/grafx2/gfx2.png $(STAGE_DIR)$(DATA)/
	cp $(S)/share/grafx2/skins/* $(STAGE_DIR)$(DATA)/skins/
	cp $(S)/share/grafx2/fonts/*.png $(S)/share/grafx2/fonts/*.gif $(S)/share/grafx2/fonts/*.pcx $(STAGE_DIR)$(DATA)/fonts/
	cp files/grafx2.dt $(DT)/types/C/
	python3 -I files/mkicons.py $(S)/share/grafx2/gfx2.png Grafx2 $(DT)/icons/C
	: > $(DT)/appmanager/C/Desktop_Apps/Grafx2
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/grafx2 $(DT)/appmanager/C/Desktop_Apps/Grafx2
	chmod -R go-w,a+rX $(STAGE_DIR)
	@$(manifest_files)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/grafx2-amix.tar etc usr

.PHONY: build stage
