# NetSurf 3.11 for AMIX: the framebuffer frontend in an X window
# (library/libnsfb's Xlib surface), FreeType with the DejaVu fonts (Comic
# Neue for cursive, Anton for fantasy, in place of Comic Sans and Impact), SVG
# through libsvgtiny, no JavaScript yet;
# HTTP(S) through curl and OpenSSL (public-key work on the Z3660's core 1).
COMPONENT_NAME    = netsurf
COMPONENT_VERSION = 3.11
COMPONENT_ARCHIVE = netsurf-$(COMPONENT_VERSION)-src.tar.gz
COMPONENT_URL     = https://download.netsurf-browser.org/netsurf/releases/source/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = c28a626aefee428d053b13f88b5c440922245976522d12eaf137cfd32d201cb2
CLEAN_FILES       = netsurf-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = netsurf-$(COMPONENT_VERSION)
R  = $(AMIX_SYSROOT)
NS_INCLUDE = $(abspath ../../../make-rules/netsurf-include)
# getopt_long from the GNU tools' compat layer (AMIX's getopt has none)
GNU_COMPAT = $(abspath ../../gnu/compat)
RES = $(PREFIX)/share/netsurf
FONTS = $(PREFIX)/share/fonts
DT    = $(STAGE_DIR)/etc/dt/appconfig

# NetSurf adds to CFLAGS and LDFLAGS with +=: ours come through the environment
NS_ENV = CFLAGS="-O2 -m68060 -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4 -U__STRICT_ANSI__ \
	-fno-split-wide-types -Wno-error -include $(CURDIR)/files/amix-netsurf.h -I$(NS_INCLUDE) -I$(GNU_COMPAT)/getopt \
	-I$(R)$(PREFIX)/include" \
	LDFLAGS="$(CURDIR)/build/libamixns.a -L$(R)$(PREFIX)/lib -Wl,-R,$(PREFIX)/lib:/usr/x11r6/lib -Wl,-u,_sys_siglist -liconv -lamixc99 -lsocket -lnsl -lgen"
# -u _sys_siglist: libamixc99's amix_str.o in libcss/libhubbub refers to it, and
# AMIX's runtime libc.so.1 has no _sys_siglist: the program must carry (and
# export) the link-time libc's copy, which it only does when asked to
NS_MAKE = $(NS_ENV) $(MAKE) -C $(S) TARGET=framebuffer HOST=AMIX PREFIX=$(PREFIX) \
	CC=m68k-elf-gcc BUILD_CC=cc PKG_CONFIG=$(PKG_CONFIG) NSSHARED=$(R)$(PREFIX)/share/netsurf-buildsystem \
	NETSURF_FB_FRONTEND=x NETSURF_FB_FONTLIB=freetype NETSURF_FB_RESPATH=$(RES)/ \
	NETSURF_FB_FONTPATH=$(FONTS)/dejavu:$(FONTS)/comic-neue:$(FONTS)/anton \
	NETSURF_FB_FONT_CURSIVE=ComicNeue-Regular.ttf NETSURF_FB_FONT_FANTASY=Anton-Regular.ttf \
	NETSURF_USE_DUKTAPE=NO NETSURF_USE_NSSVG=YES NETSURF_USE_ROSPRITE=NO NETSURF_USE_WEBP=NO \
	NETSURF_USE_JPEGXL=NO NETSURF_USE_VIDEO=NO NETSURF_USE_HARU_PDF=NO NETSURF_USE_LIBICONV_PLUG=NO \
	V=1

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	m68k-elf-gcc -O2 -m68060 -D__amix__ -c files/amix-netsurf.c -o build/amix-netsurf.o
	m68k-elf-gcc -O2 -m68060 -D__amix__ -c $(GNU_COMPAT)/amix-getopt.c -o build/amix-getopt.o
	m68k-elf-ar rcs build/libamixns.a build/amix-netsurf.o build/amix-getopt.o
	$(NS_MAKE) > build/make.log 2>&1 || { grep -E ' error|Error|undefined reference' build/make.log | head -30; exit 1; }

stage:
	rm -rf $(STAGE_DIR)
	$(NS_MAKE) install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/netsurf-fb
	# CDE: the netsurf launcher, the NetSurf action, icons, the Application
	# Manager entry; the control in the AMIX panel is desktop/cde-apps/panel
	mkdir -p $(DT)/types/C $(DT)/icons/C $(DT)/appmanager/C/Desktop_Apps
	cp files/netsurf.sh $(STAGE_DIR)$(PREFIX)/bin/netsurf
	cp files/netsurf.dt $(DT)/types/C/
	python3 -I files/mkicons.py $(S)/frontends/gtk/res/netsurf.xpm $(DT)/icons/C
	: > $(DT)/appmanager/C/Desktop_Apps/NetSurf
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/netsurf $(DT)/appmanager/C/Desktop_Apps/NetSurf
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/netsurf-amix.tar etc usr

.PHONY: build stage
