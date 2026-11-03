# libdom 0.4.2 for AMIX (NetSurf): W3C DOM; shared, with its headers
# and libdom.pc, built with NetSurf's buildsystem (make-rules/netsurf.mk).
# HTML through hubbub, XML (for SVG) through expat.
COMPONENT_NAME    = libdom
COMPONENT_VERSION = 0.4.2
COMPONENT_ARCHIVE = $(COMPONENT_NAME)-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = d05e45af16547014c2b0a3aecf3670fa13d419f505b3f5fc7ac8a1491fc30f3c
CLEAN_FILES       = $(COMPONENT_NAME)-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

# what the shared library needs
NS_LIBS           = -lhubbub -lparserutils -lwapcaplet -lexpat

include ../../../make-rules/shared.mk
include ../../../make-rules/netsurf.mk
