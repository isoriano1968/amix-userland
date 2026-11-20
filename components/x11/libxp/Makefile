# libXp, the X Print client library of X11R6.3, for AMIX: libXp.so.7, shared
# like the other X11R6.3 libraries, with xp.pc, in /usr/x11r6.  CDE links it.
#
#   gmake AMIX_SYSROOT=/path/to/sysroot stage
#
# Inputs, all pinned:
#   xc-1.tar.gz     X11R6.3 from x.org, the archive that holds xc/lib/Xp (its
#                   sha256 is x11/x11r6.3's SOURCES.sha256); the AMIX port
#                   patch does not touch lib/Xp, so the files are used as they
#                   are
#   AMIX_SYSROOT    a clean sysroot with x11/x11r6.3 in it (its headers,
#                   Print.h and Printstr.h included, and libXext/libX11.so.7)
COMPONENT_NAME    = libxp
COMPONENT_VERSION = 6.3
PREFIX            = /usr/x11r6

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

AMIX_CPU    ?= $(AMIX_CPU_BASELINE)
XC_ARCHIVE   = xc-1.tar.gz
XC_SHA256    = $(shell awk '$$2 == "$(XC_ARCHIVE)" { print $$1 }' ../x11r6.3/SOURCES.sha256)
XC_URL       = https://www.x.org/archive/X11R6.3/tars/$(XC_ARCHIVE)
# the same download cache as x11/x11r6.3
DISTFILES   ?= $(abspath ../x11r6.3/distfiles)
CLEAN_FILES  =

export PATH := $(AMIX_TOOLS):$(PATH)
CC          := m68k-elf-gcc --sysroot=$(AMIX_SYSROOT)
export CC
XI           = $(AMIX_SYSROOT)$(PREFIX)/include
SRC          = $(BUILD_DIR)/xc/lib/Xp
CFLAGS = -O2 $(AMIX_CPU) -fPIC -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
	-DSVR4 -DAMIX -D__amix__ -I$(XI) -I$(XI)/X11 -I$(XI)/X11/extensions -I$(SRC) -I$(BUILD_DIR)/xc/include/extensions

SHLIBS                   = libXp.so.7
SHLIB_ARCHIVE_libXp.so.7 = build/libXp.a
SHLIB_ARGS_libXp.so.7    = -L$(AMIX_SYSROOT)$(PREFIX)/lib -lXext -lX11 -Wl,-R,$(PREFIX)/lib \
	-L$(AMIX_SYSROOT)/usr/local/lib -lamixc99

$(DISTFILES)/$(XC_ARCHIVE):
	mkdir -p $(DISTFILES)
	curl -fsL -o $@.tmp $(XC_URL)
	echo "$(XC_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@

build: $(DISTFILES)/$(XC_ARCHIVE)
	$(SYSROOT_CHECK) "$(AMIX_SYSROOT)"
	test -f "$(XI)/X11/extensions/Print.h" || { echo "$(AMIX_SYSROOT) has no x11/x11r6.3"; exit 1; }
	test -n "$(XC_SHA256)" && echo "$(XC_SHA256)  $(DISTFILES)/$(XC_ARCHIVE)" | sha256sum -c --quiet -
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	gzip -dc $(DISTFILES)/$(XC_ARCHIVE) | tar -C $(BUILD_DIR) -xf - xc/lib/Xp xc/include/extensions/extutil.h
	for f in $(SRC)/*.c; do \
		$(CC) $(CFLAGS) -c $$f -o $(BUILD_DIR)/`basename $$f .c`.o 2>> $(BUILD_DIR)/build.log || exit 1; \
	done
	m68k-elf-ar rcs $(BUILD_DIR)/libXp.a $(BUILD_DIR)/*.o
	$(mkshlibs)

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig
	sed 's|@VERSION@|$(COMPONENT_VERSION)|' xp.pc.in > $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/xp.pc
	$(stage_finish)
	$(CPU_SCAN) $(STAGE_DIR)
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' libxp.manifest`" || \
		{ echo "the stage and libxp.manifest list different files"; exit 1; }

.PHONY: build stage
