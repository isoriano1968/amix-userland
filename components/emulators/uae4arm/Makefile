# uae4arm (Chips' uae4arm-rpi, libretro core) as a program for the Z3660's
# ARM core 1: an Amiga (A500, A600, A1200) emulated beside AMIX, picture in
# the RTG overlay, sound on HDMI.  See DESIGN.md.
COMPONENT_NAME    = uae4arm
COMPONENT_VERSION = fc1cb90
COMPONENT_ARCHIVE = uae4arm-rpi-fc1cb90.tar.gz
COMPONENT_URL     = https://github.com/Chips-fr/uae4arm-rpi/archive/fc1cb90afd6b5c6d9bb933d111c4c99c90f37688.tar.gz
COMPONENT_SHA256  = e9d9626e1af3f046adb8495a3ac79f69154e77065d593463e0a1f3faf63ca9df
CLEAN_FILES       = $(COMPONENT_ARCHIVE) $(COREMARK_TGZ)
# the benchmark disk's CoreMark: the pristine 1.01 archive (files/bench/build.sh
# checks its sha256)
COREMARK_TGZ      = coremark-1.01.tar.gz
COREMARK_URL      = https://github.com/eembc/coremark/archive/refs/tags/v1.01.tar.gz

include ../../../make-rules/shared.mk
include ../../../make-rules/z3660.mk

SRC       = $(BUILD_DIR)/src
QEMU_ARM ?= $(HOME)/opt/qemu/usr/bin/qemu-arm
# the core 1 harness (bench.h, lscript.ld) and the firmware's BSP
Z3660_CORE1 ?= $(abspath ../../../../amix-gate/usr/src/firmware/core1)
# Z3660 and ARM: make-rules/z3660.mk
ZLIB_TGZ     = $(abspath ../../library/zlib/zlib-1.3.1.tar.gz)
C1MAKE       = PATH=$(ARM):$$PATH $(MAKE) -f $(CURDIR)/files/core1/Makefile SRC=$(SRC) \
               ZLIB_TGZ=$(ZLIB_TGZ) Z3660_CORE1=$(Z3660_CORE1) Z3660=$(Z3660)

# the pristine archive, then our patches
prep: $(COMPONENT_ARCHIVE)
	rm -rf $(SRC)
	mkdir -p $(BUILD_DIR)
	tar xzf $(COMPONENT_ARCHIVE) -C $(BUILD_DIR)
	mv $(BUILD_DIR)/uae4arm-rpi-fc1cb90afd6b5c6d9bb933d111c4c99c90f37688 $(SRC)
	for p in patches/*.patch; do patch -s -p1 -d $(SRC) < $$p || exit 1; done

$(COREMARK_TGZ):
	curl -fL --retry 3 -o $@.part $(COREMARK_URL) && mv $@.part $@

$(ZLIB_TGZ):
	$(MAKE) -C ../../library/zlib zlib-1.3.1.tar.gz

# the core 1 image, the PC test build, the AMIX helper, the benchmark disk
build: $(ZLIB_TGZ) $(COREMARK_TGZ) $(Z3660_TREE)
	test -d $(SRC) || $(MAKE) prep
	$(C1MAKE) TARGET=core1 OUT=$(BUILD_DIR)/core1
	$(C1MAKE) TARGET=host OUT=$(BUILD_DIR)/host
	mkdir -p $(BUILD_DIR)/amix
	$(AMIX_CC) -O2 $(AMIX_CPU_BASELINE) -Wall -I$(Z3660_CORE1)/cores/h264bench -Ifiles/core1 \
	    files/amix/c1uae.c -o $(BUILD_DIR)/amix/c1uae
	COREMARK_TGZ=$(CURDIR)/$(COREMARK_TGZ) sh files/bench/build.sh $(BUILD_DIR)/bench

# on the PC: Kickstart + c1bench.adf under qemu-arm, CoreMark must validate
#   make check KICK=<A1200 Kickstart ROM>
check:
	@test -n "$(KICK)" || { echo "make check KICK=<A1200 Kickstart ROM>"; exit 2; }
	mkdir -p $(BUILD_DIR)/check/sys
	printf '%s\n' -var uae4arm_model=A1200 -var uae4arm_resolution=640x256 \
	    -cfg "kickstart_rom_file=$(KICK)" -frames 12000 $(BUILD_DIR)/check/sys \
	    $(BUILD_DIR)/bench/c1bench.adf > $(BUILD_DIR)/check/args
	$(QEMU_ARM) -cpu cortex-a9 $(BUILD_DIR)/host/uaehost.elf @$(BUILD_DIR)/check/args | tee $(BUILD_DIR)/check/log
	grep -q "Correct operation validated" $(BUILD_DIR)/check/log

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/root/z3660ARMCores $(STAGE_DIR)/opt/amix/bin $(STAGE_DIR)/opt/uae/bench
	cp $(BUILD_DIR)/core1/z3660ARM_Core_uae.bin $(STAGE_DIR)/root/z3660ARMCores/
	cp $(BUILD_DIR)/amix/c1uae $(STAGE_DIR)/opt/amix/bin/
	cp $(BUILD_DIR)/bench/c1bench.adf $(BUILD_DIR)/bench/coremark.060 $(BUILD_DIR)/bench/coremark.000 \
	    $(STAGE_DIR)/opt/uae/bench/
	$(manifest_files)

.PHONY: prep build check stage
