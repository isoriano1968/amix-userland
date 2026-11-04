# What a native build on AMIX needs from the C99 world besides its compilers:
# libamixc99.a, the part of a current libc that SVR4.0's lacks (snprintf and
# friends, from developer/gcc-14's compat/), with amixc99.pc, and stdint.h,
# stdbool.h and inttypes.h for AMIX's gcc 2.7 (developer/gcc-27/include).
COMPONENT_NAME    = amixc99
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

LIBAMIXC99 = $(shell $(AMIX_CC) -print-file-name=libamixc99.a)

build:
	@test -f "$(LIBAMIXC99)" || { echo "no libamixc99.a: build developer/gcc-14 first"; exit 1; }

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp $(LIBAMIXC99) $(STAGE_DIR)$(PREFIX)/lib/
	sed 's|@PREFIX@|$(PREFIX)|' files/amixc99.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/amixc99.pc
	cp ../../developer/gcc-27/include/stdint.h ../../developer/gcc-27/include/stdbool.h \
		../../developer/gcc-27/include/inttypes.h $(STAGE_DIR)$(PREFIX)/include/
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
