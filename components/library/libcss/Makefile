# libcss 0.9.2 for AMIX (NetSurf): CSS parser and selection engine; shared, with its headers
# and libcss.pc, built with NetSurf's buildsystem (make-rules/netsurf.mk).
COMPONENT_NAME    = libcss
COMPONENT_VERSION = 0.9.2
COMPONENT_ARCHIVE = $(COMPONENT_NAME)-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 2df215bbec34d51d60c1a04b01b2df4d5d18f510f1f3a7af4b80cddb5671154e
CLEAN_FILES       = $(COMPONENT_NAME)-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

# what the shared library needs
NS_LIBS           = -lparserutils -lwapcaplet

include ../../../make-rules/shared.mk
include ../../../make-rules/netsurf.mk
