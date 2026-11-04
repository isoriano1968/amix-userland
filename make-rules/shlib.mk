# Shared libraries and the end of a stage, the same for every build style.
#
# Libraries: most build systems make no shared library for AMIX, so they build
# a static archive with -fPIC and tools/amix-mkshlib makes the .so.N from it
# (GOT check, link, strip, data relocations, the lib<name>.so link).  Name
# them in SHLIBS and give each its archive and link arguments:
#
#   SHLIBS                      = libfoo.so.1
#   SHLIB_ARCHIVE_libfoo.so.1   = build/lib/.libs/libfoo.a
#   SHLIB_ARGS_libfoo.so.1      = -L$(AMIX_SYSROOT)/usr/local/lib -lbar
#   SHLIB_INIT_libfoo.so.1      = -init        (only with constructors)
#   SHLIB_STUB_ONLY_libfoo.so.1 = ^foo_        (only for a huge export list)
#
# $(mkshlibs) in the build rule makes them all in build/, each with the
# assembler for its link stub (tools/amix-link-stub): AMIX's own ld cannot
# read a library from current binutils, so on the Amiga libfoo.so is a stub
# that amix-stubs builds there with as and ld.
#
# The stage: after the install into $(STAGE_DIR), $(stage_finish) removes
# STAGE_REMOVE (paths under $(STAGE_DIR)$(PREFIX); default the libtool
# archives, static archives and CMake package files), puts SHLIBS and their
# links into $(PREFIX)/lib and the stub sources with their LIST into
# /usr/local/share/amix-stubs/<name>, makes nothing group- or world-writable,
# and packs build/<name>-amix.tar from STAGE_TOP (default usr).  amix-install
# unpacks it on the Amiga and builds the stubs.

TOOLS        ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools)
STAGE_REMOVE ?= lib/*.la lib/*.a lib/cmake
STAGE_TOP    ?= usr
STUB_DIR      = $(STAGE_DIR)/usr/local/share/amix-stubs/$(COMPONENT_NAME)

mkshlibs = $(foreach l,$(SHLIBS),$(TOOLS)/amix-mkshlib $(SHLIB_INIT_$(l)) \
	$(SHLIB_ARCHIVE_$(l)) $(l) build $(SHLIB_ARGS_$(l)) && \
	$(if $(SHLIB_STUB_ONLY_$(l)),STUB_ONLY='$(SHLIB_STUB_ONLY_$(l))') \
	$(TOOLS)/amix-link-stub build/$(l) build/$(l).s $(SHLIB_ARGS_$(l)) &&) true

define stage_finish
	cd $(STAGE_DIR)$(PREFIX) && rm -rf $(STAGE_REMOVE)
	$(if $(SHLIBS),mkdir -p $(STAGE_DIR)$(PREFIX)/lib $(STUB_DIR) && \
	  cp -P $(foreach l,$(SHLIBS),build/$(l) build/$(firstword $(subst .so., ,$(l))).so) \
	  $(STAGE_DIR)$(PREFIX)/lib/ && \
	  cp $(foreach l,$(SHLIBS),build/$(l).s) $(STUB_DIR)/ && \
	  printf '%s\n' $(foreach l,$(SHLIBS),'$(l) $(PREFIX)/lib') > $(STUB_DIR)/LIST)
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar $(STAGE_TOP)
endef
