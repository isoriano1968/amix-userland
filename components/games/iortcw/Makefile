# iortcw single player for AMIX: the 060 build with GL on the Z3660's core 1
# (zzgl, x11/mesa-183), and the whole engine on core 1 (files/core1,
# z3660ARM_Core_wolf.bin, run by games/ioquake3's c1q3; its DESIGN-core1.md).
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

# --- the whole engine on the Z3660's core 1 (games/ioquake3 DESIGN-core1.md) ---
ARM          = $(HOME)/opt/arm-none-eabi/bin
Z3660_CORE1 ?= $(abspath ../../../../amix-gate/usr/src/firmware/core1)
Z3660       ?= $(abspath ../../../../upstream/Z3660-beta21)
C1MAKE       = PATH=$(ARM):$$PATH $(MAKE) -f $(CURDIR)/files/core1/Makefile SRC=$(SRC) \
               OUT=$(BUILD_DIR)/core1 Z3660_CORE1=$(Z3660_CORE1) Z3660=$(Z3660)

# the core 1 image (the harness, glue and c1q3 are games/ioquake3's)
core1:
	test -d $(SRC) || $(MAKE) prep
	$(MAKE) -C ../../library/dlmalloc malloc-2.8.6.c
	$(C1MAKE)

# what goes on AMIX: the image and wolf.sh (c1q3 comes with games/ioquake3)
stage-core1:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/root/z3660ARMCores $(STAGE_DIR)/opt/rtcw
	cp $(BUILD_DIR)/core1/z3660ARM_Core_wolf.bin $(STAGE_DIR)/root/z3660ARMCores/
	cp files/amix/wolf.sh $(STAGE_DIR)/opt/rtcw/
	cp files/amix/install-core1.sh $(STAGE_DIR)/
	mkdir -p $(BUILD_DIR)/elf
	cp $(BUILD_DIR)/core1/wolf.elf $(BUILD_DIR)/elf/wolf-`sha256sum $(BUILD_DIR)/core1/z3660ARM_Core_wolf.bin | cut -c1-16`.elf

.PHONY: core1 stage-core1
