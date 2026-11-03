# What NetSurf's libraries' Makefiles include after shared.mk: they build
# with NetSurf's own buildsystem (developer/netsurf-buildsystem, from the
# sysroot) and install as shared libraries, lib<name>.so.<major> as the
# buildsystem names them, with their headers and .pc files.  The buildsystem's
# own shared rules do not know this target: its static archive, built -fPIC,
# becomes the library (tools/amix-mkshlib).
# A component sets COMPONENT_NAME, _VERSION, _ARCHIVE, _URL, _SHA256 and,
# for flags of its own, NS_CFLAGS; the libraries it needs, NS_LIBS; and
# NS_SO_INIT = -init when it has constructors.

# gperf (libhubbub, libsvgtiny) is a tool of the build machine: GNU gperf 3.1
# built into ~/opt/gperf when the system has none
GPERF_DIR ?= $(HOME)/opt/gperf/bin
export PATH := $(AMIX_TOOLS):$(GPERF_DIR):$(PATH)
S      = $(COMPONENT_NAME)-$(COMPONENT_VERSION)
NS_BS  = $(AMIX_SYSROOT)$(PREFIX)/share/netsurf-buildsystem
# headers AMIX lacks (strings.h), for all of them
NS_INCLUDE = $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/netsurf-include

# NetSurf's Makefiles put their own flags before $(CFLAGS): ours come
# through the environment, which a make command line would override
NS_ENV = CFLAGS="-O2 -m68060 -fPIC -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4 -U__STRICT_ANSI__ -fno-split-wide-types -Wno-error \
	-I$(NS_INCLUDE) -I$(AMIX_SYSROOT)$(PREFIX)/include $(NS_CFLAGS)" \
	LDFLAGS="-L$(AMIX_SYSROOT)$(PREFIX)/lib"
NS_MAJOR = $(firstword $(subst ., ,$(COMPONENT_VERSION)))
NS_SO    = $(COMPONENT_NAME).so.$(NS_MAJOR)
# libamixc99 last: snprintf, strncasecmp and the rest AMIX's libc lacks are
# the library's own, not left to the program
NS_LINK  = -L$(AMIX_SYSROOT)$(PREFIX)/lib $(NS_LIBS) -Wl,-R,$(PREFIX)/lib -lamixc99
NS_MAKE = $(NS_ENV) $(MAKE) -C $(S) PREFIX=$(PREFIX) NSSHARED=$(NS_BS) \
	HOST=m68k-elf CC=m68k-elf-gcc AR=m68k-elf-ar BUILD_CC=cc \
	PKGCONFIG=$(PKG_CONFIG) COMPONENT_TYPE=lib-static VARIANT=release $(NS_MAKEFLAGS)

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in $(wildcard patches/*.patch); do (cd $(S) && patch -p1 < ../$$p) || exit 1; done
	mkdir -p build
	$(NS_MAKE) > build/make.log 2>&1 || { grep -E ' error|Error|undefined reference' build/make.log | head -20; exit 1; }
	../../../tools/amix-mkshlib $(NS_SO_INIT) $(S)/build-*-lib-static/$(COMPONENT_NAME).a $(NS_SO) build $(NS_LINK)

stage:
	rm -rf $(STAGE_DIR)
	$(NS_MAKE) install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	rm $(STAGE_DIR)$(PREFIX)/lib/$(COMPONENT_NAME).a
	cp build/$(NS_SO) $(STAGE_DIR)$(PREFIX)/lib/
	ln -sf $(NS_SO) $(STAGE_DIR)$(PREFIX)/lib/$(COMPONENT_NAME).so
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
