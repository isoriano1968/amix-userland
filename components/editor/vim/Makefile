# Vim 9.1, terminal only, for AMIX; on library/ncurses in the sysroot.
COMPONENT_NAME    = vim
COMPONENT_VERSION = 9.1.0000
COMPONENT_ARCHIVE = vim-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/vim/vim/archive/refs/tags/v$(COMPONENT_VERSION).tar.gz
COMPONENT_SHA256  = 8d2a74e358be9cf260174e15e2e29dba4773f2670718b5bf728db6f54b90e25d
CLEAN_FILES       = vim-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build: $(COMPONENT_ARCHIVE)
	tar xzf $(COMPONENT_ARCHIVE)
	sh configure-amix.sh
	$(MAKE) -C vim-$(COMPONENT_VERSION)/src

# vim's own install into the stage, without the man pages in other languages
stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C vim-$(COMPONENT_VERSION)/src install DESTDIR=$(STAGE_DIR) STRIP=m68k-elf-strip > $(BUILD_DIR)/install.log 2>&1 || \
		{ tail -20 $(BUILD_DIR)/install.log; exit 1; }
	find $(STAGE_DIR)/usr/local/share/man -mindepth 1 -maxdepth 1 ! -name "man*" -exec rm -rf {} +
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf $(BUILD_DIR)/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
