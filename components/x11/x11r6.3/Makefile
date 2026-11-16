# X11R6.3 for AMIX: the client libraries as shared libraries (libX*.so.7),
# their headers and .pc files, libXtInherit's source and xauth, cross-built.
# The X server Xamix is x11/xserver-amix; the native AMIX build is README.md's.
#
#   gmake AMIX_SYSROOT=/path/to/clean/sysroot stage
#
# Inputs, all pinned:
#   xc-1/2/3.tar.gz          X11R6.3 from x.org (SOURCES.sha256, install.sh)
#   patches/0001-amix-port.patch          the AMIX port (install.sh applies it)
#   patches/cross/*.patch    the cross build's settings: MIT-SHM on, host.def
#   amix-z3660rtg            x11/cross/host.def at RTG_COMMIT (RTG_SHA256)
#   AMIX_SYSROOT             a clean sysroot: developer/amix/sysroot on
#                            OpenIndiana, the same tree made from
#                            amigaux/amix-system elsewhere.  Never one that
#                            already holds a /usr/x11r6.
#
# Tools: gcc-14 (AMIX_TOOLS) compiles and links every object; imake writes
# the Makefiles for gcc-27's names (GCC27_TOOLS); host cc and cpp build imake
# and makekeys; GNU make.
COMPONENT_NAME    = x11r6.3
COMPONENT_VERSION = 6.3

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

AMIX_CPU    ?= -m68060
GCC27_TOOLS ?= $(HOME)/opt/amix-cross/bin
HOST_CC     ?= gcc
HOST_CPP    ?= cpp

RTG_COMMIT  = 91667dec985e32cf652104c1f4028ced924e8d48
RTG_SHA256  = 3b41d6bd9a7864bbd52ac8b0b4440cf6734140234b7deaf52d2ebcc111acfdf6
RTG_ARCHIVE = amix-z3660rtg-$(RTG_COMMIT).tar.gz
RTG_URL     = https://github.com/jusii/amix-z3660rtg/archive/$(RTG_COMMIT).tar.gz

DISTFILES = $(CURDIR)/distfiles
XC        = $(BUILD_DIR)/xc
SHSTAGE   = $(BUILD_DIR)/shared
LOG       = $(BUILD_DIR)/log

$(DISTFILES)/$(RTG_ARCHIVE):
	mkdir -p $(DISTFILES)
	curl -fsL -o $@.tmp $(RTG_URL)
	echo "$(RTG_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@

# 1. the tree: pristine X11R6.3 + the AMIX port (install.sh, which checks
#    the archives), the cross patches, host.def for this sysroot.
#    install.sh does not stop on a failing patch: no .rej may be left.
$(BUILD_DIR)/.prep: SOURCES.sha256 install.sh patches/0001-amix-port.patch \
		$(wildcard patches/cross/*.patch) $(DISTFILES)/$(RTG_ARCHIVE)
	$(SYSROOT_CHECK) "$(AMIX_SYSROOT)"
	test ! -d "$(AMIX_SYSROOT)/usr/x11r6" || \
		{ echo "$(AMIX_SYSROOT) already holds usr/x11r6: build against a clean sysroot"; exit 1; }
	rm -rf "$(XC)" "$(BUILD_DIR)/amix-z3660rtg-$(RTG_COMMIT)" "$(LOG)"
	mkdir -p $(LOG)
	X11R63_DISTFILES=$(DISTFILES) sh install.sh $(BUILD_DIR) > $(LOG)/00-install.log 2>&1
	test -z "`find $(XC) -name '*.rej'`" || { echo "patch rejects in $(XC)"; exit 1; }
	cd $(BUILD_DIR) && for p in $(CURDIR)/patches/cross/*.patch; do \
		patch -p1 --no-backup-if-mismatch < $$p || exit 1; done
	tar -xzf $(DISTFILES)/$(RTG_ARCHIVE) -C $(BUILD_DIR) \
		amix-z3660rtg-$(RTG_COMMIT)/x11/cross/host.def
	rm -f $(XC)/config/cf/host.def		# read-only in the archive
	{ printf '/* written by the x11r6.3 component Makefile */\n#define AmixSysroot %s\n' "$(AMIX_SYSROOT)"; \
	  cat $(BUILD_DIR)/amix-z3660rtg-$(RTG_COMMIT)/x11/cross/host.def; } > $(XC)/config/cf/host.def
	touch $@

# 2. imake (a host program) and the Makefiles of the AMIX cross configuration;
#    makestrs, which writes Xt's generated headers (Shell.h, StringDefs.h),
#    is a host program: X11R6.3's imake cannot build host tools in a cross
#    build, so it is built here with the host compiler through its own
#    Makefile, after the core headers are exported.  "includes" still fails
#    in the server's host tools (mkg3states), which this component does not
#    need: the headers the stage takes are checked instead.
$(BUILD_DIR)/.makefiles: $(BUILD_DIR)/.prep files/imakecpp.sh
	cd $(XC)/config/imake && $(MAKE) -f Makefile.ini BOOTSTRAPCFLAGS="-DAMIX -DSVR4 -Dm68k" \
		CC="$(HOST_CC) -std=gnu89 -fcommon" > $(LOG)/01-imake.log 2>&1
	rm -f $(XC)/xmakefile	# a failed imake run leaves an empty one
	cd $(XC) && export PATH=$(GCC27_TOOLS):$$PATH AMIX_SYSROOT=$(AMIX_SYSROOT) \
		IMAKECPP=$(CURDIR)/files/imakecpp.sh HOST_CPP="$(HOST_CPP)" && \
	  $(MAKE) xmakefile > $(LOG)/02-xmakefile.log 2>&1 && \
	  $(MAKE) -f xmakefile Makefiles > $(LOG)/03-Makefiles.log 2>&1 && \
	  $(MAKE) -C include includes > $(LOG)/04-include-headers.log 2>&1 && \
	  $(MAKE) -C config/util makestrs CC="$(HOST_CC) -std=gnu89 -w" > $(LOG)/04-makestrs.log 2>&1 && \
	  { $(MAKE) -f xmakefile includes > $(LOG)/04-includes.log 2>&1 || true; }
	for h in X11/Xlib.h X11/Intrinsic.h X11/StringDefs.h X11/Shell.h X11/Xaw/Box.h X11/Xmu/Xmu.h \
		X11/ICE/ICElib.h X11/SM/SMlib.h X11/extensions/XShm.h X11/extensions/XInput.h; do \
		test -e $(XC)/exports/include/$$h || { echo "no exports/include/$$h"; exit 1; }; done
	touch $@

# 3. the shared libraries, xauth and libXtInherit.a (files/build-shared.sh)
$(BUILD_DIR)/.built: $(BUILD_DIR)/.makefiles files/build-shared.sh files/rand48.c files/xtinherit.c
	rm -rf "$(SHSTAGE)"
	AMIX_TOOLS=$(AMIX_TOOLS) AMIX_CPU=$(AMIX_CPU) AMIX_SYSROOT=$(AMIX_SYSROOT) \
		sh files/build-shared.sh $(XC) $(SHSTAGE) > $(LOG)/05-build-shared.log 2>&1
	touch $@

prep: $(BUILD_DIR)/.prep
build: $(BUILD_DIR)/.built

# 4. the stage: what goes to /usr/x11r6 on AMIX (and into a sysroot).  The
#    headers are exports/include's links resolved.  libXtInherit.a is built on
#    the Amiga by its own gcc (native.sh): gcc 14's is for GNU ld only.
#    libXp is x11/libxp.
X = $(SHSTAGE)/usr/x11r6
stage: build
	rm -rf "$(STAGE_DIR)"
	mkdir -p $(STAGE_DIR)/usr/x11r6/lib/pkgconfig $(STAGE_DIR)/usr/x11r6/include $(STAGE_DIR)/usr/x11r6/bin $(STUB_DIR)
	cp -P $(X)/lib/lib*.so.7 $(X)/lib/lib*.so $(STAGE_DIR)/usr/x11r6/lib/
	cp $(filter-out %/xp.pc,$(wildcard $(X)/lib/pkgconfig/*.pc)) $(STAGE_DIR)/usr/x11r6/lib/pkgconfig/
	cp -RL $(XC)/exports/include/X11 $(STAGE_DIR)/usr/x11r6/include/
	cp $(X)/bin/xauth $(STAGE_DIR)/usr/x11r6/bin/
	$(call link_stubs,$(STAGE_DIR)/usr/x11r6/lib/lib*.so.7,/usr/x11r6/lib,$(STAGE_DIR)/usr/x11r6/lib/lib*.so.7)
	cp files/xtinherit.c $(STUB_DIR)/
	printf '%s\n' '# libXtInherit.a by the Amiga'"'"'s own gcc' \
		'gcc -O -I/usr/x11r6/include -c $$1/xtinherit.c && ar rcs /usr/x11r6/lib/libXtInherit.a xtinherit.o' \
		> $(STUB_DIR)/native.sh
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' x11r6.3.manifest`" || \
		{ echo "the stage and x11r6.3.manifest list different files"; exit 1; }
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: prep build stage
