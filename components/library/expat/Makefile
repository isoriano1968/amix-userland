# Expat 2.7.3 for AMIX: XML parser (libexpat.so.1, shared, with its headers
# and expat.pc); libdom's XML binding uses it, for SVG in NetSurf.
COMPONENT_NAME    = expat
COMPONENT_VERSION = 2.7.3
COMPONENT_ARCHIVE = expat-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://github.com/libexpat/libexpat/releases/download/R_2_7_3/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 71df8f40706a7bb0a80a5367079ea75d91da4f8c65c58ec59bcdfbf7decdab9f

include ../../../make-rules/shared.mk

# AMIX's math.h declares isnan only when __STDC__ is 0: gcc's builtin;
# -fvisibility=default (after configure's hidden): a static build hides the
# API too, and the shared library is made from it
CONFIGURE_CFLAGS  = -O2 -fPIC -fvisibility=default -Disnan=__builtin_isnan
CONFIGURE_OPTIONS = --without-docbook --without-examples --without-tests --without-xmlwf
# the library and expat.pc only
INSTALL_TARGETS   = lib:install .:install-pkgconfigDATA
STAGE_REMOVE      = lib/*.la lib/*.a lib/cmake share

# libtool makes no shared library for this target: from the -fPIC archive
SHLIBS                      = libexpat.so.1
SHLIB_ARCHIVE_libexpat.so.1 = build/lib/.libs/libexpat.a

include ../../../make-rules/configure.mk
