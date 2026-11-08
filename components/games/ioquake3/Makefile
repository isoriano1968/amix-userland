# ioquake3 for AMIX: the 060 build with GL on the Z3660's core 1 (zzgl,
# x11/mesa-183) or soft3D, and the whole engine on core 1 (files/core1,
# z3660ARM_Core_q3.bin, run by c1q3; DESIGN-core1.md).
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
	for p in patches/*.patch; do patch -s -p1 -d $(SRC) < $$p; done
	cp -R files/amix $(SRC)/amix

.PHONY: prep

# --- the whole engine on the Z3660's core 1 (DESIGN-core1.md) ---
ARM          = $(HOME)/opt/arm-none-eabi/bin
Z3660_CORE1 ?= $(abspath ../../../../amix-gate/usr/src/firmware/core1)
Z3660       ?= $(abspath ../../../../upstream/Z3660-beta21)
C1MAKE       = PATH=$(ARM):$$PATH $(MAKE) -f $(CURDIR)/files/core1/Makefile SRC=$(SRC) \
               OUT=$(BUILD_DIR)/core1 Z3660_CORE1=$(Z3660_CORE1) Z3660=$(Z3660)
# the demo's data for the qemu test (not in git)
Q3DATA      ?= $(abspath ../../../../private/quake/data)

# the core 1 image (needs amix-gate's build/arm/libOSMesa.a: xbuild.py arm)
# and c1q3, the 060's helper
core1:
	test -d $(SRC) || $(MAKE) prep
	$(C1MAKE)
	mkdir -p $(BUILD_DIR)/amix
	$(AMIX_CC) -O2 -m68060 -Wall -Wno-implicit-int -I$(Z3660_CORE1)/cores/h264bench -Ifiles/core1 \
	    -I$(SRC)/code/client files/amix/c1q3.c -o $(BUILD_DIR)/amix/c1q3 `$(PKG_CONFIG) --cflags --libs x11`

# the real image in qemu's Zynq with fake060.py as the 68k: q3dm1 loads, a
# frame of it is drawn and the config comes back (always before a board test)
check-core1:
	rm -rf $(BUILD_DIR)/check1 && mkdir -p $(BUILD_DIR)/check1/base/demoq3 $(BUILD_DIR)/check1/home
	ln -s $(Q3DATA)/demoq3/pak0.pk3 $(BUILD_DIR)/check1/base/demoq3/pak0.pk3
	files/qemu/run.sh -base $(BUILD_DIR)/check1/base -home $(BUILD_DIR)/check1/home -secs 600 \
	    -at 10 "devmap q3dm1" -shot 240 $(BUILD_DIR)/check1/q3dm1.ppm -at 250 quit -- \
	    +set fs_basepath /base +set fs_homepath /home +set com_basegame demoq3 +set vm_cgame 0 \
	    +set vm_game 0 +set vm_ui 0 +set sv_pure 0 +set r_mode -1 +set r_customwidth 640 \
	    +set r_customheight 480 +set s_mixahead 0.5 | tee $(BUILD_DIR)/check1/log
	grep -q "CL_InitCGame:" $(BUILD_DIR)/check1/log
	test -s $(BUILD_DIR)/check1/home/demoq3/q3config.cfg

# what goes on AMIX for the core 1 game; files/amix/install-c1q3.sh puts it there
stage-core1:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/root/z3660ARMCores $(STAGE_DIR)/opt/amix/bin
	cp $(BUILD_DIR)/core1/z3660ARM_Core_q3.bin $(STAGE_DIR)/root/z3660ARMCores/
	cp $(BUILD_DIR)/amix/c1q3 $(STAGE_DIR)/opt/amix/bin/
	cp files/amix/install-c1q3.sh $(STAGE_DIR)/

.PHONY: core1 check-core1 stage-core1
