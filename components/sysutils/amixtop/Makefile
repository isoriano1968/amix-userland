# amixtop: a small curses system monitor for AMIX, on library/amixstat.
# Installed setgid sys (native.sh, run by amix-install), as the library
# reads /dev/kmem.
COMPONENT_NAME    = amixtop
COMPONENT_VERSION = 2

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
STUB_DIR = $(STAGE_DIR)/usr/local/share/amix-stubs/$(notdir $(CURDIR))

build:
	mkdir -p $(BUILD_DIR)
	m68k-elf-gcc -O2 -m68060 -D__amix__ -D__svr4__ -DSVR4 -fcommon `$(PKG_CONFIG) --cflags ncurses amixstat` -c amixtop.c -o $(BUILD_DIR)/amixtop.o
	m68k-elf-gcc $(BUILD_DIR)/amixtop.o `$(PKG_CONFIG) --libs ncurses amixstat` -o $(BUILD_DIR)/amixtop
	m68k-elf-strip $(BUILD_DIR)/amixtop
	../../../tools/amix-fix-needed $(BUILD_DIR)/amixtop > /dev/null

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STUB_DIR)
	cp $(BUILD_DIR)/amixtop $(STAGE_DIR)$(PREFIX)/bin/
	printf '%s\n' '# amixtop reads /dev/kmem: group sys, setgid (amix-install runs this)' \
		'/usr/bin/chgrp sys $(PREFIX)/bin/amixtop && /usr/bin/chmod 2755 $(PREFIX)/bin/amixtop' \
		> $(STUB_DIR)/native.sh
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
