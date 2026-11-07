# xrandr 1.0.2 (X11R7.0) for AMIX: the RandR command, /usr/x11r6/bin/xrandr.
# Lists the sizes Xzz9000 offers and switches between them (x11/xzz9000 with
# the RandR backport); the CDE display panel uses the same library calls.
COMPONENT_NAME    = xrandr
COMPONENT_VERSION = 1.0.2
COMPONENT_ARCHIVE = xrandr-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://www.x.org/releases/individual/app/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 132073c8c398ba065d50c55f350959addc8136a56f0a6885bcff3c505206d14a
PREFIX            = /usr/x11r6
S                 = xrandr-$(COMPONENT_VERSION)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)
CFLAGS = -O2 -m68060 -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
	-U__linux__ -Ulinux -DSVR4 -DAMIX -D__amix__ -Ibuild `$(PKG_CONFIG) --cflags xrandr`
LIBS = `$(PKG_CONFIG) --libs xrandr`

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	: > build/config.h
	m68k-elf-gcc $(CFLAGS) -o build/xrandr $(S)/xrandr.c $(LIBS) 2> build/build.log || \
		{ tail -20 build/build.log; exit 1; }
	m68k-elf-strip build/xrandr

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/man/man1
	cp build/xrandr $(STAGE_DIR)$(PREFIX)/bin/
	sed -e "s/__vendorversion__/X11R7.0 for AMIX/" -e "s/__appmansuffix__/1/" $(S)/xrandr.man > $(STAGE_DIR)$(PREFIX)/man/man1/xrandr.1
	$(stage_finish)
	$(manifest_files)

.PHONY: build stage
