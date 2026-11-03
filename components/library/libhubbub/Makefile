# libhubbub 0.3.8 for AMIX (NetSurf): HTML5 parser; shared, with its headers
# and libhubbub.pc, built with NetSurf's buildsystem (make-rules/netsurf.mk).
COMPONENT_NAME    = libhubbub
COMPONENT_VERSION = 0.3.8
COMPONENT_ARCHIVE = $(COMPONENT_NAME)-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 8ac1e6f5f3d48c05141d59391719534290c59cd029efc249eb4fdbac102cd5a5
CLEAN_FILES       = $(COMPONENT_NAME)-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

# what the shared library needs
NS_LIBS           = -lparserutils

include ../../../make-rules/shared.mk
include ../../../make-rules/netsurf.mk
