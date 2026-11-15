# CDE 2.2.4a for AMIX on shared libraries: the /usr/dt tree laid out by CDE's
# own install databases, libDt*.so.2, libtt.so.2 and libcsa.so.2 in
# /usr/dt/lib, CDE's headers and .pc files, and the glue libraries
# libamixcxx/libamixcde.
#
#   gmake AMIX_SYSROOT=/path/to/sysroot stage
#
# Inputs, all pinned:
#   cde-src-2.2.4a.tar.gz    from SourceForge (CDE_SHA256), into distfiles/
#   patches/cde/*.patch      the AMIX port
#   motif-2.3.8.tar.gz       desktop/motif's archive and patches/: CDE's build
#                            reads Motif's lib/Xm sources (imports/motif/lib/Xm)
#   xc-1.tar.gz              X11R6.3 (x11/x11r6.3's SOURCES.sha256): XKeysymDB
#                            and XErrorDB from xc/lib/X11
#   x11r6.3/files/xtinherit.c  libXtInherit.a, built here as x11/x11r6.3 does
#   AMIX_SYSROOT             a clean sysroot with x11/x11r6.3, x11/libxp and
#                            desktop/motif in it (developer/amix/sysroot and
#                            those packages on OpenIndiana); never one that
#                            already holds a /usr/dt
#
# Tools: gcc-14 (AMIX_TOOLS) compiles and links everything for AMIX; host cc,
# c++, cpp, flex, bison and bash build and run imake and CDE's own build-time
# tools; bdftopcf and mkfontdir make the fonts; GNU make, python3.
COMPONENT_NAME    = cde
COMPONENT_VERSION = 2.2.4a

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

AMIX_CPU ?= -m68060
HOST_CC  ?= gcc
HOST_CXX ?= g++
export AMIX_SYSROOT AMIX_TOOLS AMIX_CPU HOST_CC HOST_CXX

DISTFILES     = $(CURDIR)/distfiles
CDE_ARCHIVE   = $(DISTFILES)/cde-src-$(COMPONENT_VERSION).tar.gz
CDE_SHA256    = 63db1628daecedbe9768ef0ef8079ae02ecdd8dbd7aca63e47ef480a8e4b8b35
CDE_URL       = https://sourceforge.net/projects/cdesktopenv/files/src/cde-src-$(COMPONENT_VERSION).tar.gz/download
# desktop/motif's archive, where its Makefile keeps it
MOTIF_DIR     = $(abspath ../motif)
MOTIF_ARCHIVE = $(MOTIF_DIR)/motif-2.3.8.tar.gz
MOTIF_SHA256  = $(shell sed -n 's/^COMPONENT_SHA256 *= *//p' $(MOTIF_DIR)/Makefile)
MOTIF_URL     = $(shell sed -n 's/^COMPONENT_URL *= *//p' $(MOTIF_DIR)/Makefile)
# x11/x11r6.3's archive and download cache (as x11/libxp)
XC_DISTFILES  = $(abspath ../../x11/x11r6.3/distfiles)
XC_ARCHIVE    = $(XC_DISTFILES)/xc-1.tar.gz
XC_SHA256     = $(shell awk '$$2 == "xc-1.tar.gz" { print $$1 }' ../../x11/x11r6.3/SOURCES.sha256)
XC_URL        = https://www.x.org/archive/X11R6.3/tars/xc-1.tar.gz
export CDE_ARCHIVE MOTIF_ARCHIVE MOTIF_DIR XC_ARCHIVE

$(CDE_ARCHIVE):
	mkdir -p $(DISTFILES)
	curl -fsL -o $@.tmp '$(CDE_URL)'
	echo "$(CDE_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@
$(MOTIF_ARCHIVE):
	curl -fsL -o $@.tmp '$(MOTIF_URL)'
	echo "$(MOTIF_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@
$(XC_ARCHIVE):
	mkdir -p $(XC_DISTFILES)
	curl -fsL -o $@.tmp $(XC_URL)
	echo "$(XC_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@

# 1. the sources: CDE and Motif unpacked and patched, imports/, amix-cxxfix/,
#    this build's paths in host.def and amixpaths.def
$(BUILD_DIR)/.prep: $(CDE_ARCHIVE) $(MOTIF_ARCHIVE) $(XC_ARCHIVE) scripts/prep.sh scripts/common.sh \
		scripts/amix-cxxfix.py $(wildcard patches/cde/*.patch) $(wildcard $(MOTIF_DIR)/patches/*.patch)
	echo "$(CDE_SHA256)  $(CDE_ARCHIVE)" | sha256sum -c --quiet -
	test -n "$(MOTIF_SHA256)" && echo "$(MOTIF_SHA256)  $(MOTIF_ARCHIVE)" | sha256sum -c --quiet -
	test -n "$(XC_SHA256)" && echo "$(XC_SHA256)  $(XC_ARCHIVE)" | sha256sum -c --quiet -
	test ! -d "$(AMIX_SYSROOT)/usr/dt" || \
		{ echo "$(AMIX_SYSROOT) already holds usr/dt: build against a clean sysroot"; exit 1; }
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	sh scripts/prep.sh > $(BUILD_DIR)/prep.log 2>&1 || { tail -20 $(BUILD_DIR)/prep.log; exit 1; }
	touch $@

# 2. the shared build: imake, the host tools, CDE's libraries made shared
#    (build/shared/usr/dt/lib), the programs linked to them, the fonts
$(BUILD_DIR)/.built: $(BUILD_DIR)/.prep scripts/build-cde.sh scripts/hostbuild.sh scripts/imakecpp.sh \
		scripts/shared-cde-libs.sh $(abspath ../../x11/x11r6.3/files/xtinherit.c)
	sh scripts/build-cde.sh > $(BUILD_DIR)/build.log 2>&1 || { tail -30 $(BUILD_DIR)/build.log; exit 1; }
	touch $@

prep: $(BUILD_DIR)/.prep
build: $(BUILD_DIR)/.built

# 3. the stage: /usr/dt from CDE's install databases plus the AMIX additions
#    (package-cde.sh), the shared libraries, the development files
#    (stage-dev.sh), the link stubs of CDE's libraries; build/cde-amix.tar
#    with AMIX's plain ustar and numeric owners (AMIX-INSTALL.sh sets the real
#    ones)
DL = $(STAGE_DIR)/usr/dt/lib
# CDE_CATALOGS=no: leave out the message catalogues (/usr/dt/lib/nls/msg/*.cat,
# made by the build host's gencat).  OpenIndiana's gencat rejects CDE's
# message sources, and the catalogues belong in AMIX's own catalogue format
# anyway, which no host gencat writes yet; CDE's programs fall back to their
# built-in English texts without them.  The development files do not need them.
CDE_CATALOGS ?= yes
ifeq ($(CDE_CATALOGS),no)
MANIFEST_FILES = awk '$$1 == "file" && $$2 !~ /\.cat$$/ { print $$2 }' cde.manifest
else
MANIFEST_FILES = awk '$$1 == "file" { print $$2 }' cde.manifest
endif
stage: build
	rm -rf "$(STAGE_DIR)"
	sh scripts/package-cde.sh $(STAGE_DIR) > $(BUILD_DIR)/stage.log 2>&1 || { tail -20 $(BUILD_DIR)/stage.log; exit 1; }
	mkdir -p $(DL)
	cp -P $(BUILD_DIR)/shared/usr/dt/lib/lib*.so.2 $(BUILD_DIR)/shared/usr/dt/lib/lib*.so $(DL)/
	sh scripts/stage-dev.sh $(STAGE_DIR) >> $(BUILD_DIR)/stage.log 2>&1 || { tail -20 $(BUILD_DIR)/stage.log; exit 1; }
	$(call link_stubs,$(DL)/lib*.so.[12],/usr/dt/lib,$(DL)/lib*.so.[12] $(AMIX_SYSROOT)/usr/motif/lib/lib*.so.4 $(AMIX_SYSROOT)/usr/x11r6/lib/lib*.so.7)
	chmod -R go-w $(STAGE_DIR)
	$(if $(filter no,$(CDE_CATALOGS)),find $(STAGE_DIR)/usr/dt/lib/nls/msg -name '*.cat' -exec rm -f {} +)
	@$(MANIFEST_FILES) | while read f; do [ -e "$(STAGE_DIR)$$f" ] || [ -L "$(STAGE_DIR)$$f" ] || \
		{ echo "manifest: $$f not in the stage"; exit 1; }; done
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`$(MANIFEST_FILES) | wc -l`" || \
		{ echo "the stage and cde.manifest list different files"; exit 1; }
	tar --format=ustar --numeric-owner --owner=2 --group=2 -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr
	@test ! -s $(STAGE_DIR)/../MISSING || echo "not built (expected, see README): `wc -l < $(STAGE_DIR)/../MISSING` files, listed in build/MISSING"

.PHONY: prep build stage
