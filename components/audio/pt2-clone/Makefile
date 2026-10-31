# 8bitbubsy's ProTracker 2.3D clone for AMIX, on SDL 2 and Paula.  Upstream's
# tree carries Windows and macOS binaries, so it is fetched, not kept here.
COMPONENT_NAME    = pt2-clone
COMPONENT_VERSION = 18954f2
COMPONENT_ARCHIVE = pt2-clone-18954f23c3d0ebd8583e61b65fe4507ae8aff4bd.tar.gz
COMPONENT_URL     = https://github.com/8bitbubsy/pt2-clone/archive/18954f23c3d0ebd8583e61b65fe4507ae8aff4bd.tar.gz
COMPONENT_SHA256  = da236c2ae600684351d86f186242e3b4eebf421badaaa68eee884efe7f523b23
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

SRC = $(BUILD_DIR)/pt2-clone-18954f23c3d0ebd8583e61b65fe4507ae8aff4bd

$(SRC)/amix/build.sh: $(COMPONENT_ARCHIVE)
	mkdir -p $(BUILD_DIR)
	tar xzf $(COMPONENT_ARCHIVE) -C $(BUILD_DIR)
	patch -s -p1 -d $(SRC) < patches/0001-amix.patch
	cp -R files/amix $(SRC)/amix

build: $(SRC)/amix/build.sh
	cd $(SRC)/amix && sh build.sh

.PHONY: build
