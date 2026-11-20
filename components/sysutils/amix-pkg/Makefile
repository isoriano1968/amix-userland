# pkg, the AMIX IPS client (doc/plans/IPS-CLIENT-DESIGN.md).
#   make build   the AMIX binary (gcc 14 cross, zlib; its own HTTP, no TLS)
#   make test    a host build, its unit tests and tests/run.sh against tests/repo
#   make stage   build/stage/usr/bin/pkg
COMPONENT_NAME    = amix-pkg
COMPONENT_VERSION = 0.5.3

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
SRC      = $(filter-out src/amix-hw.c,$(wildcard src/*.c))
HW_SRC   = src/amix-hw.c src/hw.c src/util.c
HDR      = src/pkg.h
HOSTCC  ?= cc
HOSTFLAGS = -O2 -g -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -DPKG_TESTING -Isrc

build: $(BUILD_DIR)/pkg $(BUILD_DIR)/amix-hw

# 68020 to 68060: pkg runs on every AMIX machine, an A3000 with its 030 too
# (the AMIX baseline, doc/CPU-TARGET-68020-60.md)
CFLAGS68 = -O2 $(AMIX_CPU_BASELINE) -D__amix__ -D__svr4__ -DSVR4 -Wall -Isrc
OBJ68    = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(SRC))
HW_OBJ68 = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(HW_SRC))

$(BUILD_DIR)/obj/%.o: src/%.c $(HDR)
	mkdir -p $(BUILD_DIR)/obj
	m68k-elf-gcc $(CFLAGS68) `$(PKG_CONFIG) --cflags zlib` -c $< -o $@

# libamixc99 (snprintf and friends) linked in, from an archive of its own:
# pkg is the installer's bootstrap and the first package on a stock AMIX,
# which has no /usr/lib/libamixc99.so.1 yet (library/amixc99 brings it).  The
# archive is built here from developer/gcc-14's compat/ sources with the
# baseline flags; --as-needed drops the shared libamixc99.so.1 the compiler
# would add.
COMPAT     = ../../developer/gcc-14/compat
COMPAT_SRC = $(filter-out $(COMPAT)/amix_init.c,$(wildcard $(COMPAT)/*.c))
AMIXC99_A  = $(BUILD_DIR)/compat/libamixc99.a

$(AMIXC99_A): $(COMPAT_SRC)
	rm -rf $(BUILD_DIR)/compat && mkdir -p $(BUILD_DIR)/compat
	for s in $(COMPAT_SRC); do \
		m68k-elf-gcc $(AMIX_CPU_BASELINE) -O2 -fno-strict-aliasing \
			-U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
			-c $$s -o $(BUILD_DIR)/compat/`basename $$s .c`.o || exit 1; \
	done
	m68k-elf-ar rcs $@ $(BUILD_DIR)/compat/*.o

$(BUILD_DIR)/pkg: $(OBJ68) $(AMIXC99_A)
	../../../tools/amix-fpu-scan -q $(OBJ68)
	m68k-elf-gcc $(AMIX_CPU_BASELINE) $(OBJ68) $(AMIXC99_A) -Wl,--as-needed \
		`$(PKG_CONFIG) --libs zlib` -lsocket -lnsl -lgen \
		-Wl,-rpath-link,$(AMIX_SYSROOT)/usr/lib -Wl,-rpath-link,$(AMIX_SYSROOT)/usr/local/lib \
		-o $@
	@if m68k-elf-readelf -d $@ | grep -q libamixc99; then echo "$@ still needs libamixc99.so"; exit 1; fi
	m68k-elf-strip $@
	../../../tools/amix-fix-needed $@ > /dev/null
	../../../tools/amix-fix-needed $@ > /dev/null

# amix-hw: the boards (hw.c, which pkg has too), for scripts and the CDE session
$(BUILD_DIR)/amix-hw: $(HW_OBJ68) $(AMIXC99_A)
	m68k-elf-gcc $(AMIX_CPU_BASELINE) $(HW_OBJ68) $(AMIXC99_A) -Wl,--as-needed \
		-Wl,-rpath-link,$(AMIX_SYSROOT)/usr/lib -o $@
	@if m68k-elf-readelf -d $@ | grep -q libamixc99; then echo "$@ still needs libamixc99.so"; exit 1; fi
	m68k-elf-strip $@

test: $(SRC) $(HDR) tests/unit.c
	mkdir -p $(BUILD_DIR)/host
	$(HOSTCC) $(HOSTFLAGS) $(SRC) -lz -o $(BUILD_DIR)/host/pkg
	$(HOSTCC) $(HOSTFLAGS) $(HW_SRC) -o $(BUILD_DIR)/host/amix-hw
	$(HOSTCC) $(HOSTFLAGS) tests/unit.c $(filter-out src/main.c,$(SRC)) -lz -o $(BUILD_DIR)/host/unit
	$(BUILD_DIR)/host/unit
	sh tests/run.sh $(BUILD_DIR)/host/pkg $(BUILD_DIR)/host/amix-hw

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/bin
	cp $(BUILD_DIR)/pkg $(BUILD_DIR)/amix-hw $(STAGE_DIR)/usr/bin/
	mkdir -p $(STAGE_DIR)/etc/dt/config/Xsession.d
	cp files/0013.amix-hw $(STAGE_DIR)/etc/dt/config/Xsession.d/
	chmod 755 $(STAGE_DIR)/etc/dt/config/Xsession.d/0013.amix-hw
	$(CPU_SCAN) $(STAGE_DIR)

.PHONY: build test stage
