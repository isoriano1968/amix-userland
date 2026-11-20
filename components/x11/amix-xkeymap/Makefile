# The Amiga keyboard layouts under X: Xamix's built-in keymap is US only;
# xkeymap-amiga gives the server the console's layout (the map
# /usr/amiga/lib/kmap/default.kmap names, set with amixkeymap) as an xmodmap
# table, at every CDE session (/usr/dt/config/Xsession.d/0015.amix-keymap) and
# on the login screen (desktop/cde's Xsetup).
#
# The tables (files/xmodmap.amiga-*) are made by tools/kmap2xmodmap.py from
# AMIX's console keymaps, which are Commodore's files on the Amiga and never
# part of this tree.  The build checks them: with KMAP_DIR (a folder of AMIX's
# /usr/amiga/lib/kmap, e.g. from your own core disk) every table is made again
# and must be the same.
COMPONENT_NAME    = amix-xkeymap
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

KMAP_DIR ?= $(wildcard ../../../../usr/amiga/lib/kmap)
X11      = /usr/x11r6/lib/X11

build:
	mkdir -p $(BUILD_DIR)
	@if [ -n "$(KMAP_DIR)" ]; then \
		for t in files/xmodmap.amiga-*; do \
			n=`basename $$t | sed 's/^xmodmap.amiga-//'`; \
			python3 -I ../../../tools/kmap2xmodmap.py $(KMAP_DIR)/$$n $$n > $(BUILD_DIR)/check.$$n || exit 1; \
			cmp -s $(BUILD_DIR)/check.$$n $$t || { echo "$$t differs from kmap2xmodmap.py $(KMAP_DIR)/$$n"; exit 1; }; \
		done; \
		echo "amix-xkeymap: every table made again from $(KMAP_DIR): the same"; \
	else \
		echo "amix-xkeymap: no KMAP_DIR: the tables are taken as they are"; \
	fi

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(X11) $(STAGE_DIR)/opt/amix/bin $(STAGE_DIR)/usr/dt/config/Xsession.d
	cp files/xmodmap.amiga-* $(STAGE_DIR)$(X11)/
	cp files/xkeymap-amiga $(STAGE_DIR)/opt/amix/bin/
	cp files/0015.amix-keymap $(STAGE_DIR)/usr/dt/config/Xsession.d/
	chmod 644 $(STAGE_DIR)$(X11)/xmodmap.amiga-*
	chmod 755 $(STAGE_DIR)/opt/amix/bin/xkeymap-amiga $(STAGE_DIR)/usr/dt/config/Xsession.d/0015.amix-keymap
	chmod -R go-w,a+rX $(STAGE_DIR)
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' amix-xkeymap.manifest`" || \
		{ echo "the stage and amix-xkeymap.manifest list different files"; exit 1; }
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr opt

.PHONY: build stage
