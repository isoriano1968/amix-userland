# libXp, the X Print client library of X11R6.3, for AMIX: static, as the AMIX
# build of X11R6.3 installs only a shared one.  From the prepared X11R6.3 tree.
COMPONENT_NAME    = libxp
COMPONENT_VERSION = 6.3

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
X11_SRC ?= $(HOME)/Sources/usr-amix/private/xzz9000/src/xc
X = $(AMIX_SYSROOT)/usr/x11r6/xc/exports
CFLAGS = -O2 -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
	-DSVR4 -DAMIX -D__amix__ -I$(X)/include -I$(X)/include/X11 -I$(X)/include/X11/extensions \
	-I$(X11_SRC)/include/extensions -I$(X11_SRC)/lib/Xp

build:
	@test -d $(X11_SRC)/lib/Xp || { echo "no $(X11_SRC)/lib/Xp: set X11_SRC"; exit 1; }
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	for f in $(X11_SRC)/lib/Xp/*.c; do \
		m68k-elf-gcc $(CFLAGS) -c $$f -o $(BUILD_DIR)/`basename $$f .c`.o 2>> $(BUILD_DIR)/build.log || exit 1; \
	done
	m68k-elf-ar rcs $(BUILD_DIR)/libXp.a $(BUILD_DIR)/*.o

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/x11r6/lib/pkgconfig
	cp $(BUILD_DIR)/libXp.a $(STAGE_DIR)/usr/x11r6/lib/
	sed 's|@VERSION@|$(COMPONENT_VERSION)|' xp.pc.in > $(STAGE_DIR)/usr/x11r6/lib/pkgconfig/xp.pc

.PHONY: build stage
