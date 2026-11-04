# gnu.mk -- what a GNU tool's component shares: configure for AMIX with gcc 14
# and a stage in /usr/gnu.  A component sets the COMPONENT_ variables, PROGS (its
# programs, relative to the build), EXTRA_CONF, and COMPAT (of getopt and wide, the
# headers of ../compat it needs), then includes this.
GNU := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
# /usr/gnu, where GNU tools do not shadow AMIX's own; a tool AMIX does not
# have sets GNU_PREFIX = /usr/local before the include (gnu/gzip)
GNU_PREFIX ?= /usr/gnu
PREFIX = $(GNU_PREFIX)
S = $(COMPONENT_NAME)-$(COMPONENT_VERSION)
W = -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
	-Wno-error=incompatible-pointer-types -Wno-error=implicit-int
CC_BASE = m68k-elf-gcc -m68060 -std=gnu99 -D__amix__ -DSVR4 -w $(W)
# -fno-split-wide-types: gcc 14 m68k makes a sub.l of two memory operands without it
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -std=gnu99 $(COMPAT:%=-I$(GNU)/compat/%) -DO_CLOEXEC=0 -U__linux__ -Ulinux -D__amix__ -DSVR4 -w $(W)

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do [ ! -f $$p ] || patch -s -d $(S) -p1 < $$p; done
	mkdir -p build
	$(CC_BASE) -O2 -DAMIX_PROG=\"$(COMPONENT_NAME)\" -c $(GNU)/compat/amix-gnu.c -o build/amix-gnu.o
	$(CC_BASE) -O2 -c $(GNU)/compat/amix-getopt.c -o build/amix-getopt.o
	$(CC_BASE) -O2 -c $(GNU)/compat/amix-printf.c -o build/amix-printf.o
	m68k-elf-ar rcs build/libamixgnu.a build/amix-gnu.o build/amix-getopt.o build/amix-printf.o
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fno-split-wide-types" LIBS="-L$(CURDIR)/build -lamixgnu -lamixc99 $(EXTRA_LIBS)" LDFLAGS=-Wl,--allow-multiple-definition \
		ac_cv_func_getprogname=yes ac_cv_func_strtoll=yes ac_cv_func_strtoull=yes $(EXTRA_ENV) \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=$(PREFIX) --disable-nls --disable-year2038 $(EXTRA_CONF) > configure.log 2>&1
	# the forced header only now: configure takes what cc -E prints for flags
	$(MAKE) -C build -j8 CFLAGS="-O2 -fno-split-wide-types -include $(GNU)/compat/amix-gnu.h $(EXTRA_CFLAGS)" $(MAKE_TARGET) > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin
	for p in $(PROGS); do cp build/$$p $(STAGE_DIR)$(PREFIX)/bin/ && m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/`basename $$p`; done
	$(STAGE_EXTRA)

.PHONY: build stage
