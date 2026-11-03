# libparserutils 0.2.5 for AMIX (NetSurf): parser building blocks (input streams, character sets); shared, with its headers
# and libparserutils.pc, built with NetSurf's buildsystem (make-rules/netsurf.mk).
COMPONENT_NAME    = libparserutils
COMPONENT_VERSION = 0.2.5
COMPONENT_ARCHIVE = $(COMPONENT_NAME)-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/libs/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 317ed5c718f17927b5721974bae5de32c3fd6d055db131ad31b4312a032ed139
CLEAN_FILES       = $(COMPONENT_NAME)-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

# what the shared library needs
NS_LIBS           = -liconv

include ../../../make-rules/shared.mk
include ../../../make-rules/netsurf.mk
