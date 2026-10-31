# What every component's Makefile includes.
AMIX_TOOLS ?= $(HOME)/opt/m68k-amix/bin
AMIX_CC    ?= $(AMIX_TOOLS)/m68k-elf-gcc

# out of the source tree, one per component; git ignores them
BUILD_DIR  = $(CURDIR)/build
STAGE_DIR  = $(CURDIR)/build/stage

clean:
	rm -rf build $(CLEAN_FILES)

.PHONY: clean

# the source: fetched into the component, checked against COMPONENT_SHA256
$(COMPONENT_ARCHIVE):
	curl -fL --retry 3 -o $@.part $(COMPONENT_URL)
	echo "$(COMPONENT_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@
