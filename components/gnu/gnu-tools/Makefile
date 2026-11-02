# The GNU tools as a set: nothing of its own, it builds what its manifest requires
# and puts their stages together in build/stage and build/gnu-tools.tar.
COMPONENT_NAME    = gnu-tools
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

TOOLS = $(shell sed -n 's|^require  ||p' gnu-tools.manifest)

build:
	for t in $(TOOLS); do $(MAKE) -C ../../$$t build || exit 1; done

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	for t in $(TOOLS); do $(MAKE) -C ../../$$t stage && cp -R ../../$$t/build/stage/. $(STAGE_DIR)/ || exit 1; done
	tar --format=ustar --numeric-owner --owner=0 --group=0 -C $(STAGE_DIR) -cf $(BUILD_DIR)/gnu-tools.tar usr

.PHONY: build stage
