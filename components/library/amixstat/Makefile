# libamixstat: what the running AMIX system is doing (memory, swap, CPU,
# processes, file systems, network interfaces, the Z3660 core 1's network), for amixsysmon and amixtop,
# and the amixstat command that prints it.  Ours: no download.  The command
# is installed setgid sys, as the library reads /dev/kmem.
COMPONENT_NAME    = amixstat
COMPONENT_VERSION = 1.1

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
CC     = m68k-elf-gcc -m68060
CFLAGS = -O2 -Wall

SHLIBS                          = libamixstat.so.1
SHLIB_ARCHIVE_libamixstat.so.1  = build/libamixstat.a

build:
	mkdir -p build
	$(CC) $(CFLAGS) -fPIC -c files/amixstat.c -o build/amixstat.o
	m68k-elf-ar rcs build/libamixstat.a build/amixstat.o
	$(mkshlibs)
	$(CC) $(CFLAGS) -Ifiles -c files/amixstat-cmd.c -o build/amixstat-cmd.o
	$(CC) -o build/amixstat build/amixstat-cmd.o -Lbuild -lamixstat -Wl,-R,$(PREFIX)/lib
	m68k-elf-strip build/amixstat
	../../../tools/amix-fix-needed build/amixstat

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/include $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	cp build/amixstat $(STAGE_DIR)$(PREFIX)/bin/
	cp files/amixstat.h $(STAGE_DIR)$(PREFIX)/include/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' files/amixstat.pc.in \
		> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/amixstat.pc
	mkdir -p $(STUB_DIR)
	printf '%s\n' '# amixstat reads /dev/kmem: group sys, setgid (amix-install runs this)' \
		'/usr/bin/chgrp sys $(PREFIX)/bin/amixstat && /usr/bin/chmod 2755 $(PREFIX)/bin/amixstat' \
		> $(STUB_DIR)/native.sh
	$(stage_finish)

.PHONY: build stage
