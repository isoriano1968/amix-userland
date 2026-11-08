# dlmalloc 2.8.6 (Doug Lea, public domain): the malloc of the Z3660's core 1
# programs, in place of newlib's.  newlib carries the old 2.6 that takes no
# memory below its top, so a core 1 image could not use both of its heap
# regions (below the image and above it); 2.8.6 with a non-contiguous
# MORECORE keeps them as separate segments.  Nothing goes on AMIX: core 1
# images (games/ioquake3 files/core1) compile the pristine file with their
# own settings (their Makefiles name them).
COMPONENT_NAME    = dlmalloc
COMPONENT_VERSION = 2.8.6
COMPONENT_ARCHIVE = malloc-2.8.6.c
COMPONENT_URL     = http://gee.cs.oswego.edu/pub/misc/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 103602c3fcbe200d5e257cdd7353d84bcc033d887bea3b245321319bf5401f47
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

build: $(COMPONENT_ARCHIVE)

.PHONY: build
