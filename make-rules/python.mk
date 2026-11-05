# Pure-Python packages for the system Python 3.11 (runtime/python3), each its
# own component under components/library/python/<name>, as Debian's python3-*
# or OpenIndiana's library/python/*.  The upstream download is the release's
# py3-none-any wheel from PyPI, pinned by SHA-256: those files are the same on
# every machine.  tools/amix-wheel-install puts it into the stage as pip would
# install it on AMIX (site-packages, console scripts, INSTALLER, RECORD).
#
# A recipe sets COMPONENT_NAME, COMPONENT_VERSION, COMPONENT_ARCHIVE (the
# wheel's file name: shared.mk's download rule needs it before this file),
# COMPONENT_SHA256 and COMPONENT_URL (its files.pythonhosted.org link), includes shared.mk
# and this file; STAGE_EXTRA runs after the install (extra files from files/).
#
#   make build   fetch and check the wheel
#   make stage   install into build/stage, pack build/<name>-amix.tar
#
# Example: components/library/python/pip.

CLEAN_FILES       ?= $(COMPONENT_ARCHIVE)
PY_TOOLS          := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../tools)

build: $(COMPONENT_ARCHIVE)

stage: $(COMPONENT_ARCHIVE)
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	python3 $(PY_TOOLS)/amix-wheel-install $(COMPONENT_ARCHIVE) $(STAGE_DIR)
	$(STAGE_EXTRA)
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
