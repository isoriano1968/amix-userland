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

.PHONY: build
