# fsv 0.9 for AMIX: the 3D file system visualizer (a clone of IRIX's fsn),
# with a Motif interface in place of GTK+, drawn by Mesa 18.3 on the Z3660.
# fsv in /usr/local/bin (a launcher that loads the GPU program on core 1;
# the program is /usr/local/libexec/fsv), its manual in /usr/local/share/fsv, a CDE action.
COMPONENT_NAME    = fsv
COMPONENT_VERSION = 4ef9040
COMPONENT_COMMIT  = 4ef9040fcf14cddd52d79dad87a80355fc885602
COMPONENT_ARCHIVE = fsv-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/mcuelenaere/fsv/archive/$(COMPONENT_COMMIT).tar.gz
COMPONENT_SHA256  = 6dccdbe5e602b9580c55d24a41807c888e71334084782e22bd66bd8a92c10c3c
CLEAN_FILES       = fsv-$(COMPONENT_COMMIT) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S     = fsv-$(COMPONENT_COMMIT)
DOC   = $(PREFIX)/share/fsv
DT    = $(STAGE_DIR)/etc/dt/appconfig

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -p1 < $$p || exit 1; done
	cp files/config.h $(S)/
	$(MAKE) -C $(S) -f $(CURDIR)/files/Makefile.motif \
		CC="m68k-elf-gcc -m68060" CFLAGS="-O2 -fno-split-wide-types -std=gnu99 -w" \
		EXTRA_CPPFLAGS="-I$(CURDIR)/files/amix -include $(CURDIR)/files/amix/amix-fsv.h -U__linux__ -D__amix__ -DSVR4 -DHAVE_STRING_H -DDOCDIR=\\\"$(DOC)\\\"" \
		EXTRA_SRC="lib/scandir.c $(CURDIR)/files/amix/fnmatch.c" \
		XCFLAGS="$$($(PKG_CONFIG) --cflags xm gl glu)" \
		XLIBS="$$($(PKG_CONFIG) --libs xm gl glu)"
	mkdir -p build
	cp $(S)/fsv build/
	m68k-elf-strip build/fsv
	python3 -I files/mkhelp.py $(S)/doc build/fsv.txt

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/libexec $(STAGE_DIR)$(DOC) $(DT)/types/C $(DT)/icons/C $(DT)/appmanager/C/Desktop_Apps
	cp build/fsv $(STAGE_DIR)$(PREFIX)/libexec/
	cp files/fsv.sh $(STAGE_DIR)$(PREFIX)/bin/fsv
	cp build/fsv.txt $(STAGE_DIR)$(DOC)/
	cp files/fsv.dt $(DT)/types/C/
	python3 -I files/mkicons.py $(S)/src/xmaps/fsv-icon.xpm $(DT)/icons/C
	: > $(DT)/appmanager/C/Desktop_Apps/Fsv
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/fsv $(STAGE_DIR)$(PREFIX)/libexec/fsv $(DT)/appmanager/C/Desktop_Apps/Fsv
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/fsv-amix.tar etc usr

.PHONY: build stage
