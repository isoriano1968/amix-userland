# What every component's Makefile includes.
AMIX_TOOLS ?= $(HOME)/opt/m68k-amix/bin
AMIX_CC    ?= $(AMIX_TOOLS)/m68k-elf-gcc

# out of the source tree, one per component; git ignores them
BUILD_DIR  = $(CURDIR)/build
STAGE_DIR  = $(CURDIR)/build/stage

clean:
	rm -rf build

.PHONY: clean
