# What every program for AMIX needs from the C99 world besides its compilers:
# libamixc99.so.1, the part of a current libc that SVR4.0's lacks (snprintf
# and friends, usleep, strcasecmp, __cxa_atexit, from developer/gcc-14's
# compat/, built by its tools/build-compat.sh), with amixc99.pc, and
# stdint.h, stdbool.h and inttypes.h for AMIX's gcc 2.7
# (developer/gcc-27/include).
#
# A shared library in /usr/lib, beside libc.so.1: AMIX's ld.so.1 searches
# /usr/lib and nothing else by default, and gcc 14's specs make a program or
# library that calls these functions NEED it, so it has to load without a
# runpath, from an rc script as from a shell.  The lib<name>.so link and the
# link stub (amix-stubs on AMIX) are for links; gcc 2.7 programs take
# -lamixc99 from amixc99.pc.
#
# Version 2 is the shared library; version 1 was the archive libamixc99.a,
# linked into every program.  Programs and libraries built since need 2
# (CDE 2.2.4a 2.1-26: xhold could not start without it).
COMPONENT_NAME    = amixc99
COMPONENT_VERSION = 2

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

LIBAMIXC99 = $(shell $(AMIX_CC) -print-file-name=libamixc99.so.1)

build:
	@test -f "$(LIBAMIXC99)" || { echo "no libamixc99.so.1: build developer/gcc-14 first (tools/build-compat.sh)"; exit 1; }

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/lib $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include
	cp $(LIBAMIXC99) $(STAGE_DIR)/usr/lib/libamixc99.so.1
	chmod 755 $(STAGE_DIR)/usr/lib/libamixc99.so.1
	ln -sf libamixc99.so.1 $(STAGE_DIR)/usr/lib/libamixc99.so
	sed 's|@PREFIX@|$(PREFIX)|' files/amixc99.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/amixc99.pc
	cp ../../developer/gcc-27/include/stdint.h ../../developer/gcc-27/include/stdbool.h \
		../../developer/gcc-27/include/inttypes.h $(STAGE_DIR)$(PREFIX)/include/
	$(stage_stubs)
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' amixc99.manifest`" || \
		{ echo "the stage and amixc99.manifest list different files"; exit 1; }
	$(CPU_SCAN) $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
