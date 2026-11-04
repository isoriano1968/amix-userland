# amix-install and amix-stubs: how a package from this tree goes onto AMIX,
# with the link stubs of its shared libraries built there by AMIX's own as
# and ld.  Ours; install it first, with GNU tar:
#   cd / && /usr/gnu/bin/tar --no-overwrite-dir -xf amix-pkgtools-amix.tar
COMPONENT_NAME    = amix-pkgtools
COMPONENT_VERSION = 1.0

include ../../../make-rules/shared.mk

build:
	sh -n files/amix-install && sh -n files/amix-stubs

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/local/bin $(STAGE_DIR)/usr/local/share/amix-stubs
	install -m 755 files/amix-install files/amix-stubs $(STAGE_DIR)/usr/local/bin/
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
