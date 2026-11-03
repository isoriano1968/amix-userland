# libnsfb 0.2.2 for AMIX (NetSurf): the frame buffer library with an X
# surface on plain Xlib (patches/0001); shared, with its headers and
# libnsfb.pc, built with NetSurf's buildsystem (make-rules/netsurf.mk).
COMPONENT_NAME    = libnsfb
COMPONENT_VERSION = 0.2.2
COMPONENT_ARCHIVE = $(COMPONENT_NAME)-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = be446ca3eb54df903f2da98311d107d7574cd11f5ac0713e619156d4d19ea39a
CLEAN_FILES       = $(COMPONENT_NAME)-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

# what the shared library needs
NS_LIBS           = $(shell $(PKG_CONFIG) --libs x11 xext)
# the X surface registers itself in a constructor
NS_SO_INIT        = -init

include ../../../make-rules/shared.mk
include ../../../make-rules/netsurf.mk
