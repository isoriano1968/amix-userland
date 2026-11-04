# Components whose upstream builds with Autotools (./configure).  The recipe
# sets the COMPONENT_ variables, includes shared.mk and this file, and names
# its options; the rest is here:
#
#   CONFIGURE_OPTIONS   added to the cross defaults below
#   CONFIGURE_ENV       variables for configure, e.g. ac_cv_... answers
#   CONFIGURE_CFLAGS    default -O2 -fPIC (a static archive the .so is made from)
#   CONFIGURE_SCRIPT    default $(S)/configure
#   BUILD_POST          extra build commands, after make, before $(mkshlibs)
#   INSTALL_TARGETS     what the stage installs: "dir:target" in build/,
#                       default ".:install"
#   SHLIBS ...          shared libraries (shlib.mk)
#
#   make build   unpack, patch, configure in build/, make, shared libraries
#   make stage   install into build/stage, finish (shlib.mk), pack the tar
#
# Example: components/library/expat.

RULES := $(dir $(lastword $(MAKEFILE_LIST)))
include $(RULES)unpack.mk
include $(RULES)shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

AMIX_CPPFLAGS    ?= -m68060 -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4
CONFIGURE_CC     ?= m68k-elf-gcc $(AMIX_CPPFLAGS)
CONFIGURE_CFLAGS ?= -O2 -fPIC
CONFIGURE_SCRIPT ?= $(S)/configure
CONFIGURE_CROSS   = --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) \
	--disable-shared --enable-static
INSTALL_TARGETS  ?= .:install

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	cd build && $(CONFIGURE_ENV) CC="$(CONFIGURE_CC)" CFLAGS="$(CONFIGURE_CFLAGS)" \
		../$(CONFIGURE_SCRIPT) $(CONFIGURE_CROSS) $(CONFIGURE_OPTIONS) \
		> configure.log 2>&1 || { tail -20 configure.log; exit 1; }
	$(MAKE) -C build -j8 > build/make.log 2>&1 || \
		{ grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	$(BUILD_POST)
	$(mkshlibs)

stage:
	rm -rf $(STAGE_DIR)
	for t in $(INSTALL_TARGETS); do \
		$(MAKE) -C build/$${t%%:*} $${t#*:} DESTDIR=$(STAGE_DIR) >> build/install.log 2>&1 || \
			{ tail -20 build/install.log; exit 1; }; \
	done
	$(stage_finish)

.PHONY: build stage
