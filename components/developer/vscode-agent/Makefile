# vscode-agent: runs programs from VS Code on AMIX (README.md).
#   make build   the AMIX binary (gcc 14 cross, 68020 to 68060)
#   make test    a host build driven by the extension's protocol code
#   make stage   build/stage: the binary, the rc script and its links,
#                the example configuration
COMPONENT_NAME    = vscode-agent
COMPONENT_VERSION = 0.1.2

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
SRC      = src/agent.c src/sha256.c
HDR      = src/agent.h
OBJ      = $(BUILD_DIR)/agent.o $(BUILD_DIR)/sha256.o
HOSTCC  ?= cc
HOSTFLAGS = -O2 -g -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Isrc

build: $(BUILD_DIR)/vscode-agent

# 68020 to 68060: the agent runs on every AMIX machine, an A3000 with its 030
# too (the AMIX baseline, doc/CPU-TARGET-68020-60.md)
CFLAGS68 = -O2 $(AMIX_CPU_BASELINE) -D__amix__ -D__svr4__ -DSVR4 -Wall -Isrc

# libamixc99 (snprintf, vsnprintf) linked in, from an archive of its own, so
# the agent needs nothing beyond a stock AMIX: one file to copy for a quick
# test.  Built here from developer/gcc-14's compat/ sources with the
# baseline flags (library/amixc99 ships only the shared libamixc99.so.1);
# --as-needed drops the shared one the compiler would add.  As sysutils/amix-pkg.
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

$(BUILD_DIR)/vscode-agent: $(SRC) $(HDR) $(AMIXC99_A)
	mkdir -p $(BUILD_DIR)
	for f in $(SRC); do m68k-elf-gcc $(CFLAGS68) -c $$f -o $(BUILD_DIR)/`basename $$f .c`.o || exit 1; done
	# the agent's own code (libamixc99's switch tables give false movep hits)
	../../../tools/amix-fpu-scan -q $(OBJ)
	m68k-elf-gcc $(AMIX_CPU_BASELINE) -o $@ $(OBJ) $(AMIXC99_A) -Wl,--as-needed -lsocket -lnsl \
		-Wl,-rpath-link,$(AMIX_SYSROOT)/usr/lib
	@if m68k-elf-readelf -d $@ | grep -q libamixc99; then echo "$@ still needs libamixc99.so"; exit 1; fi
	m68k-elf-strip $@
	../../../tools/amix-fix-needed $@ > /dev/null

$(BUILD_DIR)/host/vscode-agent: $(SRC) $(HDR)
	mkdir -p $(BUILD_DIR)/host
	$(HOSTCC) $(HOSTFLAGS) $(SRC) -o $@

test: $(BUILD_DIR)/host/vscode-agent
	node tests/protocol-test.js $(BUILD_DIR)/host/vscode-agent
	node tests/target-test.js $(BUILD_DIR)/host/vscode-agent

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/local/bin $(STAGE_DIR)/etc/init.d \
		$(STAGE_DIR)/etc/rc2.d $(STAGE_DIR)/etc/rc0.d
	cp $(BUILD_DIR)/vscode-agent $(STAGE_DIR)/usr/local/bin/vscode-agent
	chmod 755 $(STAGE_DIR)/usr/local/bin/vscode-agent
	cp files/vscode-agent.rc $(STAGE_DIR)/etc/init.d/vscode-agent
	chmod 744 $(STAGE_DIR)/etc/init.d/vscode-agent
	ln $(STAGE_DIR)/etc/init.d/vscode-agent $(STAGE_DIR)/etc/rc2.d/S87vscode-agent
	ln $(STAGE_DIR)/etc/init.d/vscode-agent $(STAGE_DIR)/etc/rc0.d/K13vscode-agent
	cp files/vscode-agent.conf.example $(STAGE_DIR)/etc/vscode-agent.conf.example
	chmod 644 $(STAGE_DIR)/etc/vscode-agent.conf.example

.PHONY: build test stage
