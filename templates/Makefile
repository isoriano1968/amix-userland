# <what it is, in a line>
COMPONENT_NAME    = <name>
COMPONENT_VERSION = <version>
COMPONENT_ARCHIVE = <name>-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = <where upstream publishes it>
COMPONENT_SHA256  = <sha256 of the archive>

include ../../../make-rules/shared.mk

build: $(COMPONENT_ARCHIVE)

# what goes on AMIX, at its AMIX paths, in $(STAGE_DIR)
stage:
	rm -rf $(STAGE_DIR)

.PHONY: build stage
