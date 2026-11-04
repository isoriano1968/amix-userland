# Components whose upstream builds with Meson.  A cross file for AMIX is
# written to build/amix-cross.ini (the compiler, the sysroot's pkg-config, the
# AMIX defines, static libraries built -fPIC); Meson configures build/meson and
# ninja builds it.  The recipe sets:
#
#   MESON_OPTIONS    -D... for the project, added to the defaults below
#   MESON_SOURCE     default $(S): the directory with meson.build
#   MESON_CFLAGS     default -O2 (c_args, after the AMIX defines)
#   BUILD_POST       extra build commands, after the build, before $(mkshlibs)
#   SHLIBS ...       shared libraries (shlib.mk)
#
#   make build   unpack, patch, meson setup, ninja, shared libraries
#   make stage   meson's install into build/stage, finish (shlib.mk), the tar
#
# Example: components/library/zstd.

RULES := $(dir $(lastword $(MAKEFILE_LIST)))
include $(RULES)unpack.mk
include $(RULES)shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

MESON_SOURCE  ?= $(S)
MESON_CFLAGS  ?= -O2
MESON_DEFAULTS = --cross-file=$(CURDIR)/build/amix-cross.ini --prefix=$(PREFIX) \
	--buildtype=plain -Ddefault_library=static -Db_staticpic=true -Db_pie=false

# the cross file: meson's own idea of the machine, and where everything is
mlist = [$(foreach a,$(1),'$(a)',)]
define meson_cross
[binaries]
c = '$(AMIX_TOOLS)/m68k-elf-gcc'
cpp = '$(AMIX_TOOLS)/m68k-elf-g++'
ar = '$(AMIX_TOOLS)/m68k-elf-ar'
strip = '$(AMIX_TOOLS)/m68k-elf-strip'
pkg-config = '$(PKG_CONFIG)'

[built-in options]
c_args = $(call mlist,-m68060 -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4 $(MESON_CFLAGS))
cpp_args = $(call mlist,-m68060 -U__linux__ -Ulinux -D__amix__ -DSVR4 $(MESON_CFLAGS))

[properties]
sys_root = '$(AMIX_SYSROOT)'

[host_machine]
system = 'amix'
cpu_family = 'm68k'
cpu = '68060'
endian = 'big'
endef
export meson_cross

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	printf '%s\n' "$$meson_cross" > build/amix-cross.ini
	meson setup build/meson $(MESON_SOURCE) $(MESON_DEFAULTS) $(MESON_OPTIONS) \
		> build/meson.log 2>&1 || { tail -20 build/meson.log; exit 1; }
	ninja -C build/meson > build/make.log 2>&1 || \
		{ grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	$(BUILD_POST)
	$(mkshlibs)

stage:
	rm -rf $(STAGE_DIR)
	DESTDIR=$(STAGE_DIR) meson install -C build/meson --no-rebuild > build/install.log 2>&1 || \
		{ tail -20 build/install.log; exit 1; }
	$(stage_finish)

.PHONY: build stage
