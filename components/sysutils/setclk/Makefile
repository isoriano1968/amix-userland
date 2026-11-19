# setclk: Commodore's battery clock tool, from AMIX's own source
# (amigaux/amix-system usr/amiga/src/cmd/setclk at AMIX_SYSTEM_COMMIT) with
# patches/: a four-digit year, so the date is right at boot after 1999, and
# -t SECONDS (UTC since 1970) to set the system time to the second.
#   make build   the AMIX binary (gcc 14 cross, AMIX_CPU: 68020 code tuned for the 060)
#   make stage   build/stage/usr/amiga/bin/setclk
COMPONENT_NAME    = setclk
COMPONENT_VERSION = 2.1.2

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
# the flexible target, until make-rules/shared.mk sets AMIX_CPU for every component
AMIX_CPU ?= -mcpu=68020 -mtune=68060 -fexcess-precision=fast

AMIX_SYSTEM_COMMIT = debb2e14bce281995d22bd5607a8db1afd4db677
SRC_PATH           = usr/amiga/src/cmd/setclk/setclk.c
SRC_URL            = https://raw.githubusercontent.com/amigaux/amix-system/$(AMIX_SYSTEM_COMMIT)/$(SRC_PATH)
SRC_SHA256         = 03a00a434cff9137eaca276dab073cacd1003c0a0e2eff9493c058086b104901
SRC_FILE           = setclk-$(AMIX_SYSTEM_COMMIT).c
SRC_DIR            = $(BUILD_DIR)/src
CLEAN_FILES        = $(SRC_FILE)

$(SRC_FILE):
	curl -fL --retry 3 -o $@.part $(SRC_URL)
	echo "$(SRC_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@

$(SRC_DIR)/.patched: $(SRC_FILE) $(wildcard patches/*.patch)
	rm -rf $(SRC_DIR)
	mkdir -p $(SRC_DIR)/$(dir $(SRC_PATH))
	cp $(SRC_FILE) $(SRC_DIR)/$(SRC_PATH)
	for p in patches/*.patch; do patch -d $(SRC_DIR) -p1 < $$p || exit 1; done
	touch $@

build: $(BUILD_DIR)/setclk

# an A3000's own 68030 needs it most: the flexible target (shared.mk AMIX_CPU)
$(BUILD_DIR)/setclk: $(SRC_DIR)/.patched
	m68k-elf-gcc -O2 $(AMIX_CPU) -D__amix__ -D__svr4__ -DSVR4 -Wall \
		$(SRC_DIR)/$(SRC_PATH) -o $@
	m68k-elf-strip $@
	../../../tools/amix-fix-needed $@ > /dev/null

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/amiga/bin
	cp $(BUILD_DIR)/setclk $(STAGE_DIR)/usr/amiga/bin/setclk
	chmod 755 $(STAGE_DIR)/usr/amiga/bin/setclk
	$(manifest_files)

.PHONY: build stage
