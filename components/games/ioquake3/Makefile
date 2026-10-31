# ioquake3 for AMIX, GL on the Z3660's core 1 (x11/mesa-183) or soft3D.
COMPONENT_NAME    = ioquake3
COMPONENT_VERSION = 87a99d1
COMPONENT_ARCHIVE = ioquake3-87a99d1.tar.gz
COMPONENT_URL     = https://github.com/ioquake/ioq3/archive/87a99d18206e878a20289f05ac3c16005dcae5c3.tar.gz
COMPONENT_SHA256  = 8c0c2cdc2326a5f303afe09914b445609780f1ff9337fab5224afc33607bf720
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

SRC = $(BUILD_DIR)/ioq3-87a99d18206e878a20289f05ac3c16005dcae5c3

# the patched source with amix/; build there with amix/build.py
prep: $(COMPONENT_ARCHIVE)
	mkdir -p $(BUILD_DIR)
	tar xzf $(COMPONENT_ARCHIVE) -C $(BUILD_DIR)
	patch -s -p1 -d $(SRC) < patches/0001-amix.patch
	cp -R files/amix $(SRC)/amix

.PHONY: prep
