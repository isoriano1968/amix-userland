# libsvgtiny 0.1.8 for AMIX (NetSurf): SVG Tiny renderer; shared, with its headers
# and libsvgtiny.pc, built with NetSurf's buildsystem (make-rules/netsurf.mk).
COMPONENT_NAME    = libsvgtiny
COMPONENT_VERSION = 0.1.8
COMPONENT_ARCHIVE = $(COMPONENT_NAME)-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = c357227f02e83fb2a76b12b901191a082229db1f007362e8f31c754510c2a01c
CLEAN_FILES       = $(COMPONENT_NAME)-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

# lroundf, ceilf, fabsf, isnan: C99 math AMIX's libm lacks
NS_CFLAGS         = -include amix-c99math.h

# what the shared library needs
NS_LIBS           = -ldom -lwapcaplet -lm

include ../../../make-rules/shared.mk
include ../../../make-rules/netsurf.mk
