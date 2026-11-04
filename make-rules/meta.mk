# Metapackages: no files of their own.  The require lines of the manifest
# name the members, as in OpenIndiana's meta-packages; the Makefile sets
# COMPONENT_NAME and COMPONENT_VERSION, includes shared.mk and then this.
#
#   make build   builds every member
#   make stage   stages every member and puts the stages together in
#                build/stage (tools/amix-meta-merge: two members with
#                different files at one path stop it), packed as
#                build/<name>-amix.tar.  The members' link stubs come with
#                it, so amix-install builds them all on the Amiga.
#
# Example: components/meta/amix-dev.

RULES     := $(dir $(lastword $(MAKEFILE_LIST)))
TOOLS     ?= $(abspath $(RULES)../tools)
COMPONENTS = $(abspath $(RULES)../components)
MEMBERS    = $(shell sed -n 's|^require  *||p' $(wildcard *.manifest))

build:
	for m in $(MEMBERS); do $(MAKE) -C $(COMPONENTS)/$$m build || exit 1; done

stage:
	for m in $(MEMBERS); do $(MAKE) -C $(COMPONENTS)/$$m stage || exit 1; done
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	$(TOOLS)/amix-meta-merge $(STAGE_DIR) $(foreach m,$(MEMBERS),$(COMPONENTS)/$(m)/build/stage)
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar `ls $(STAGE_DIR)`

.PHONY: build stage
