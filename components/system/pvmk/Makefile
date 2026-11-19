# pvmk (SORIX-ILOM-LDOM): the Amiga 3000 boot loader and paravirtualising
# microkernel that boots AMIX from its own PVMK partition (pvmk.hdf), for the
# host: the AMIX Installer writes pvmk.hdf from these files.  Runs on the host
# (OpenIndiana, Linux), not on AMIX: the m68k part is boot code for the
# Amiga, the tools are host programs.
#
# Source: github.com/isoriano1968/pvmk at PVMK_COMMIT, with patches/:
#   0001  builds with the AMIX m68k-elf gcc 14 (developer/gcc-14) for the same
#         image as with m68k-linux-gnu: -mno-align-int -mno-strict-align, the
#         OS macros undefined; wc -c / shell arithmetic in place of GNU stat
#         and gawk.  Upstream as soon as it is committed there; then the pin
#         moves and the patch goes.
# Linked without libgcc (LIBGCC=): pvmk uses no helper, and the toolchain's
# libgcc is built for the 68060; the link fails if one is ever needed.
# pvmk's own "check" runs in the build: 68060-safe instructions, the boot
# block and image size budgets, the hole, the ADF verify.
#
#   make build        pvmk for the A3000 (68030/040/060) and the host tools
#   make host-stage   build/stage$(HOST_PREFIX): share/pvmk, lib/pvmk
#   make install      DESTDIR=... HOST_PREFIX=... (for packaging)
#
# Installed (HOST_PREFIX /usr for the OpenIndiana package):
#   share/pvmk/bootblock.bin, pvmk.bin, VERSION   the boot block, the image
#   share/pvmk/conf/pvmk.conf, README.disk         the template the installer
#                                                  edits, the volume's README
#   lib/pvmk/mkadf, mkhdf                          the boot volume and the RDB
#                                                  disk (host programs)
COMPONENT_NAME    = pvmk
COMPONENT_VERSION = 2026.11.8
PVMK_COMMIT       = 79c08e13c4cc733b7b8f2029adc106abd59769f1
COMPONENT_ARCHIVE = pvmk-$(PVMK_COMMIT).tar.gz
COMPONENT_URL     = https://github.com/isoriano1968/pvmk/archive/$(PVMK_COMMIT).tar.gz
COMPONENT_SHA256  = dadf400e5311e234fcf8c03eab76e5c8cac893d6a196b953e5fa4aa8fd871328
S                 = pvmk-$(PVMK_COMMIT)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk

HOST_PREFIX ?= /usr/local
HOST_STAGE   = $(STAGE_DIR)$(HOST_PREFIX)
HOSTCC      ?= cc
PYTHON      ?= python3
# the archive has no git: pvmk's version string is the commit, and the build
# date (__DATE__, mkadf's timestamps) the commit's, so the image is the same
# on every build
PVMK_VERSION = $(shell echo $(PVMK_COMMIT) | cut -c1-7)
PVMK_EPOCH   = 1794132672
PVMK_B       = $(BUILD_DIR)/pvmk
PVMK_MAKE    = SOURCE_DATE_EPOCH=$(PVMK_EPOCH) $(MAKE) -C $(S) B=$(PVMK_B) \
	CROSS=$(AMIX_TOOLS)/m68k-elf- LIBGCC= HOSTCC="$(HOSTCC)" PYTHON=$(PYTHON) \
	VERSION=$(PVMK_VERSION)

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	$(PVMK_MAKE) all
	$(PVMK_MAKE) check $(PVMK_B)/host/mkhdf

host-stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(HOST_STAGE)/share/pvmk/conf $(HOST_STAGE)/lib/pvmk
	cp $(PVMK_B)/bootblock.bin $(PVMK_B)/pvmk.bin $(HOST_STAGE)/share/pvmk/
	echo $(PVMK_VERSION) > $(HOST_STAGE)/share/pvmk/VERSION
	cp $(S)/conf/pvmk.conf $(S)/conf/README.disk $(HOST_STAGE)/share/pvmk/conf/
	cp $(PVMK_B)/host/mkadf $(PVMK_B)/host/mkhdf $(HOST_STAGE)/lib/pvmk/
	chmod 444 $(HOST_STAGE)/share/pvmk/*.bin $(HOST_STAGE)/share/pvmk/VERSION \
		$(HOST_STAGE)/share/pvmk/conf/*
	chmod 555 $(HOST_STAGE)/lib/pvmk/*

install: host-stage
	mkdir -p $(DESTDIR)$(HOST_PREFIX)
	cp -R $(HOST_STAGE)/. $(DESTDIR)$(HOST_PREFIX)/

.PHONY: build host-stage install
