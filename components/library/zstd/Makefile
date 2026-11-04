# Zstandard 1.5.7 for AMIX: libzstd.so.1 (shared, with zstd.h, zdict.h,
# zstd_errors.h and libzstd.pc), the library only.  Built with upstream's
# Meson build (build/meson) through make-rules/meson.mk, the example of a
# Meson component.  Single-threaded: AMIX has no kernel threads.  No legacy
# (pre-1.0, 2015) formats: their decoders mix U32 types on AMIX (uint32_t is
# unsigned long).
COMPONENT_NAME    = zstd
COMPONENT_VERSION = 1.5.7
COMPONENT_ARCHIVE = zstd-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/facebook/zstd/releases/download/v$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3

include ../../../make-rules/shared.mk

MESON_SOURCE  = $(S)/build/meson
MESON_CFLAGS  = -O2 -fno-split-wide-types
MESON_OPTIONS = -Dbin_programs=false -Dbin_tests=false -Dbin_contrib=false \
	-Dmulti_thread=disabled -Dzlib=disabled -Dlzma=disabled -Dlz4=disabled \
	-Dbacktrace=disabled -Ddebug_level=0 -Dlegacy_level=0

SHLIBS                    = libzstd.so.1
SHLIB_ARCHIVE_libzstd.so.1 = build/meson/lib/libzstd.a
SHLIB_ARGS_libzstd.so.1    = -L$(AMIX_SYSROOT)/usr/local/lib -lamixc99
# the stub: the API only, the internal exports are too many for AMIX's as
SHLIB_STUB_ONLY_libzstd.so.1 = ^(ZSTD_|ZDICT_)

include ../../../make-rules/meson.mk
