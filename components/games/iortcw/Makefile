# iortcw single player for AMIX, GL on the Z3660's core 1 (x11/mesa-183).
COMPONENT_NAME    = iortcw
COMPONENT_VERSION = 438e7d4
COMPONENT_ARCHIVE = iortcw-438e7d4.tar.gz
COMPONENT_URL     = https://github.com/iortcw/iortcw/archive/438e7d413b5f7277187c35b032eb0ef9093ae778.tar.gz
COMPONENT_SHA256  = e13b6e777f60163b2e0dbfd23be23606f7f3b933dcf0440b67d6457d7dc4d024
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

SRC = $(BUILD_DIR)/iortcw-438e7d413b5f7277187c35b032eb0ef9093ae778/SP

# the patched source with amix/; build there with amix/build.py
prep: $(COMPONENT_ARCHIVE)
	mkdir -p $(BUILD_DIR)
	tar xzf $(COMPONENT_ARCHIVE) -C $(BUILD_DIR)
	for p in patches/*.patch; do patch -s -p1 -d $(SRC) < $$p; done
	cp -R files/amix $(SRC)/amix

.PHONY: prep
