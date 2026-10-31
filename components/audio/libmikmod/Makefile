# libmikmod 3.1.23 and the mikmod 3.2.9 player for AMIX.  Built on AMIX with its
# gcc; the sources are the port's, in libmikmod/ and mikmod/ (see README.md).
COMPONENT_NAME    = libmikmod
COMPONENT_VERSION = 3.1.23

include ../../../make-rules/shared.mk

build:
	$(MAKE) -C libmikmod
	$(MAKE) -C mikmod

install:
	$(MAKE) -C libmikmod install
	$(MAKE) -C mikmod install

.PHONY: build install
