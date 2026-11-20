# What every component's Makefile includes.
AMIX_TOOLS ?= $(HOME)/opt/m68k-amix/bin
AMIX_CC    ?= $(AMIX_TOOLS)/m68k-elf-gcc

# libraries are found through pkg-config, in the sysroot, at their AMIX paths
AMIX_SYSROOT ?= $(HOME)/opt/m68k-amix-sysroot
# an exported but empty AMIX_SYSROOT counts as unset: "?=" keeps an empty
# value, and sysroot-install would then copy the stage over the host's /
ifeq ($(strip $(AMIX_SYSROOT)),)
override AMIX_SYSROOT := $(HOME)/opt/m68k-amix-sysroot
endif
PKG_CONFIG    = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools/amix-pkg-config)
SYSROOT_CHECK = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools/amix-sysroot-check)
export AMIX_SYSROOT

# The CPU every AMIX object can run on: 68020/030 with a 68881/2 up to the
# 68060, tuned for the 060 (doc/CPU-TARGET-68020-60.md: integer code is
# byte-identical to -m68060, FP code differs only in mnemonics).  Components
# take it as AMIX_CPU ?= $(AMIX_CPU_BASELINE); CPU_SCAN is the gate: 68040/060-only
# instructions in a staged file fail the stage.
AMIX_CPU_BASELINE = -mcpu=68020 -mtune=68060 -fexcess-precision=fast
CPU_SCAN = OBJDUMP=$(AMIX_TOOLS)/m68k-elf-objdump $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools/amix-cpu-scan)

# out of the source tree, one per component; git ignores them
BUILD_DIR  = $(CURDIR)/build
STAGE_DIR  = $(CURDIR)/build/stage

# where a component belongs on AMIX: /usr/local, /usr/gnu, /usr/X, /usr
PREFIX     ?= /usr/local

# where the builds of the shared X, Motif, zzgl and CDE libraries put them
# (x11/x11r6.3/files/build-shared.sh and the others); their components'
# stages take their part from it
SHARED_STAGE ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../private/shared-build/stage)

# a manifest's requirements are checked before anything is built
ifneq ($(wildcard *.manifest),)
build: requires
endif
requires:
	$(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools/amix-requires) $(CURDIR)

clean:
	rm -rf build $(CLEAN_FILES)

# a component's stage target fills $(STAGE_DIR) with what goes on AMIX
# (the check refuses an AMIX_SYSROOT that is, or lies inside, the host's
# system directories, one without the sysroot marker, and running on AMIX)
sysroot-install: stage
	$(SYSROOT_CHECK) -w "$(AMIX_SYSROOT)"
	cp -R $(STAGE_DIR)/. "$(AMIX_SYSROOT)"/

.PHONY: clean sysroot-install requires

# $(manifest_files): every "file" path in the manifest must be in the stage
manifest_files = awk '$$1 == "file" { print $$2 }' *.manifest | \
	while read f; do [ -e "$(STAGE_DIR)$$f" ] || { echo "manifest: $$f not in the stage"; exit 1; }; done

# the source: fetched into the component, checked against COMPONENT_SHA256
$(COMPONENT_ARCHIVE):
	curl -fL --retry 3 -o $@.part $(COMPONENT_URL)
	echo "$(COMPONENT_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@
