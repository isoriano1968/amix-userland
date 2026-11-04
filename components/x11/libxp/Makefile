# libXp, the X Print client library of X11R6.3, for AMIX: libXp.so.7, shared
# like the other X11R6.3 libraries here, with xp.pc.  From the prepared
# X11R6.3 tree; the library is made by make-rules/shlib.mk.
COMPONENT_NAME    = libxp
COMPONENT_VERSION = 6.3
PREFIX            = /usr/x11r6

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
X11_SRC ?= $(HOME)/Sources/usr-amix/private/xzz9000/src/xc
X = $(AMIX_SYSROOT)/usr/x11r6/xc/exports
CFLAGS = -O2 -fPIC -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
	-DSVR4 -DAMIX -D__amix__ -I$(X)/include -I$(X)/include/X11 -I$(X)/include/X11/extensions \
	-I$(X11_SRC)/include/extensions -I$(X11_SRC)/lib/Xp

SHLIBS                   = libXp.so.7
SHLIB_ARCHIVE_libXp.so.7 = build/libXp.a
SHLIB_ARGS_libXp.so.7    = -L$(AMIX_SYSROOT)$(PREFIX)/lib -lXext -lX11 -Wl,-R,$(PREFIX)/lib \
	-L$(AMIX_SYSROOT)/usr/local/lib -lamixc99

build:
	@test -d $(X11_SRC)/lib/Xp || { echo "no $(X11_SRC)/lib/Xp: set X11_SRC"; exit 1; }
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	for f in $(X11_SRC)/lib/Xp/*.c; do \
		m68k-elf-gcc $(CFLAGS) -c $$f -o $(BUILD_DIR)/`basename $$f .c`.o 2>> $(BUILD_DIR)/build.log || exit 1; \
	done
	m68k-elf-ar rcs $(BUILD_DIR)/libXp.a $(BUILD_DIR)/*.o
	$(mkshlibs)

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	sed 's|@VERSION@|$(COMPONENT_VERSION)|' xp.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/xp.pc
	$(stage_finish)

.PHONY: build stage
