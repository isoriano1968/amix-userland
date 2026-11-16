# CPython 3.11.18 for AMIX; the steps are in README.md.
COMPONENT_NAME    = python3
COMPONENT_VERSION = 3.11.18

include ../../../make-rules/shared.mk

# the IPS stage: build (tools/build-python-amix.sh), the install tree
# (tools/stage-python-install.py), then its payload with the links of
# generated/INSTALL-LINKS ("hard": a hard link, else a symlink)
$(BUILD_DIR)/.python-built: tools/build-python-amix.sh tools/patch-python-amix.py $(wildcard patches/*.patch)
	bash tools/build-python-amix.sh
	mkdir -p $(BUILD_DIR) && touch $@

build: $(BUILD_DIR)/.python-built

stage: build
	rm -rf build/python-install		# the stage script wants a fresh one
	python3 tools/stage-python-install.py
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	cp -a build/python-install/payload/. $(STAGE_DIR)/
	while read dest target kind; do \
		d=$(STAGE_DIR)$$dest; rm -f $$d; \
		if [ "$$kind" = hard ]; then ln `dirname $$d`/$$target $$d; else ln -s $$target $$d; fi; \
	done < build/python-install/generated/INSTALL-LINKS
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	@$(manifest_files)

.PHONY: build stage
