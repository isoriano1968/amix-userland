# pkg, the AMIX IPS client (doc/plans/IPS-CLIENT-DESIGN.md).
#   make build   the AMIX binary (gcc 14 cross, zlib; its own HTTP, no TLS)
#   make test    a host build, its unit tests and tests/run.sh against tests/repo
#   make stage   build/stage/usr/bin/pkg
COMPONENT_NAME    = amix-pkg
COMPONENT_VERSION = 0.3

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
SRC      = $(wildcard src/*.c)
HDR      = src/pkg.h
HOSTCC  ?= cc
HOSTFLAGS = -O2 -g -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -DPKG_TESTING -Isrc

build: $(BUILD_DIR)/pkg

# 68020 to 68060: pkg runs on every AMIX machine, an A3000 with its 030 too
CFLAGS68 = -O2 -m68020-60 -D__amix__ -D__svr4__ -DSVR4 -Wall -Isrc
OBJ68    = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(SRC))

$(BUILD_DIR)/obj/%.o: src/%.c $(HDR)
	mkdir -p $(BUILD_DIR)/obj
	m68k-elf-gcc $(CFLAGS68) `$(PKG_CONFIG) --cflags zlib` -c $< -o $@

$(BUILD_DIR)/pkg: $(OBJ68)
	../../../tools/amix-fpu-scan -q $(OBJ68)
	m68k-elf-gcc -m68020-60 $(OBJ68) `$(PKG_CONFIG) --libs zlib` -lsocket -lnsl -lgen \
		-Wl,-rpath-link,$(AMIX_SYSROOT)/usr/lib -Wl,-rpath-link,$(AMIX_SYSROOT)/usr/local/lib \
		-o $@
	m68k-elf-strip $@
	../../../tools/amix-fix-needed $@ > /dev/null
	../../../tools/amix-fix-needed $@ > /dev/null

test: $(SRC) $(HDR) tests/unit.c
	mkdir -p $(BUILD_DIR)/host
	$(HOSTCC) $(HOSTFLAGS) $(SRC) -lz -o $(BUILD_DIR)/host/pkg
	$(HOSTCC) $(HOSTFLAGS) tests/unit.c $(filter-out src/main.c,$(SRC)) -lz -o $(BUILD_DIR)/host/unit
	$(BUILD_DIR)/host/unit
	sh tests/run.sh $(BUILD_DIR)/host/pkg

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/bin
	cp $(BUILD_DIR)/pkg $(STAGE_DIR)/usr/bin/pkg

.PHONY: build test stage
