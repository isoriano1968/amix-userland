# GLQuake and software Quake for AMIX, from id's GPL source; the game's data is your own.
COMPONENT_NAME    = quake
COMPONENT_VERSION = 1.09

include ../../../make-rules/shared.mk

build:
	$(MAKE) -f Makefile.amix
	$(MAKE) -f Makefile.amix-sw
	$(MAKE) -f Makefile.amix-zzgl

.PHONY: build
