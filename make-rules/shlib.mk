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
# $(mkshlibs) in the build rule makes them all in build/.
#
# Link stubs: AMIX's own ld cannot read a library from current binutils, so
# on the Amiga libfoo.so is a stub that amix-stubs builds there with as and
# ld.  The stage writes the assembler for each (tools/amix-link-stub, from the
# library and its SHLIB_ARGS).
#
# Every library goes through tools/amix-fix-needed again in the stage (it
# changes nothing on one in order), whatever built it.
#
# The stage: after the install into $(STAGE_DIR), $(stage_finish) removes
# STAGE_REMOVE (paths under $(STAGE_DIR)$(PREFIX); default the libtool
# archives, static archives and CMake package files), puts SHLIBS and their
# links into $(PREFIX)/lib and the stub sources with their LIST into
# /usr/local/share/amix-stubs/<component directory>, makes nothing group- or
# world-writable, and packs build/<name>-amix.tar from STAGE_TOP (default
# usr).  amix-install unpacks it on the Amiga and builds the stubs.

TOOLS        ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools)
STAGE_REMOVE ?= lib/*.la lib/*.a lib/cmake
STAGE_TOP    ?= usr
STUB_DIR      = $(STAGE_DIR)/usr/local/share/amix-stubs/$(notdir $(CURDIR))

mkshlibs = $(foreach l,$(SHLIBS),$(TOOLS)/amix-mkshlib $(SHLIB_INIT_$(l)) \
	$(SHLIB_ARCHIVE_$(l)) $(l) build $(SHLIB_ARGS_$(l)) &&) true
shlib_stubs = $(foreach l,$(SHLIBS),$(if $(SHLIB_STUB_ONLY_$(l)),STUB_ONLY='$(SHLIB_STUB_ONLY_$(l))') \
	$(TOOLS)/amix-link-stub build/$(l) $(STUB_DIR)/$(l).s $(SHLIB_ARGS_$(l)) > /dev/null &&) true

# $(call link_stubs,LIBS,DIR,KNOWN): stubs for libraries linked elsewhere
# (X, Motif, Mesa): LIBS are staged .so.N files that go into DIR on AMIX,
# KNOWN the shared libraries whose symbols do not count.  STUB_ONLY in the
# environment narrows a library's stub as SHLIB_STUB_ONLY does.
link_stubs = mkdir -p $(STUB_DIR) && for l in $(1); do b=`basename $$l`; \
	$(TOOLS)/amix-fix-needed $$l > /dev/null && $(TOOLS)/amix-link-stub $$l $(STUB_DIR)/$$b.s -k $(3) > /dev/null && \
	echo "$$b $(2)" >> $(STUB_DIR)/LIST || exit 1; done

# $(stage_stubs): the stub of every shared library in the stage, at the end of
# the stage of a component that links its libraries without $(mkshlibs)
# (zlib, ncurses, openssl ...).  What the libraries it NEEDs define, in the
# stage or the sysroot, does not count as needs.
STUB_KNOWN  = $(foreach d,local/lib local/gnu/lib x11r6/lib motif/lib dt/lib,-n $(STAGE_DIR)/usr/$(d) -n $(AMIX_SYSROOT)/usr/$(d))
stage_stubs = mkdir -p $(STUB_DIR) && rm -f $(STUB_DIR)/LIST && \
	for l in `cd $(STAGE_DIR) && find usr -name 'lib*.so.*' -type f ! -name '*.s'`; do \
	b=`basename $$l`; $(TOOLS)/amix-fix-needed $(STAGE_DIR)/$$l > /dev/null && \
	$(TOOLS)/amix-link-stub $(STAGE_DIR)/$$l $(STUB_DIR)/$$b.s $(STUB_KNOWN) > /dev/null && \
	echo "$$b /`dirname $$l`" >> $(STUB_DIR)/LIST || exit 1; done

define stage_finish
	cd $(STAGE_DIR)$(PREFIX) && rm -rf $(STAGE_REMOVE)
	$(if $(SHLIBS),mkdir -p $(STAGE_DIR)$(PREFIX)/lib $(STUB_DIR) && \
	  cp -P $(foreach l,$(SHLIBS),build/$(l) build/$(firstword $(subst .so., ,$(l))).so) \
	  $(STAGE_DIR)$(PREFIX)/lib/ && \
	  $(TOOLS)/amix-fix-needed $(foreach l,$(SHLIBS),$(STAGE_DIR)$(PREFIX)/lib/$(l)) > /dev/null && \
	  $(shlib_stubs) && \
	  printf '%s\n' $(foreach l,$(SHLIBS),'$(l) $(PREFIX)/lib') > $(STUB_DIR)/LIST)
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar $(STAGE_TOP)
endef
