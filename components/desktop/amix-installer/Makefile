# AMIX Installer: a GTK 4 / libadwaita app on the host (OpenIndiana, Linux),
# not on AMIX.  Python 3.9+, PyGObject, GTK >= 4.12, libadwaita >= 1.4.
#   make build        icons from doc/assets/amix-logo.png, byte-compile check
#   make test         unit tests of the model, profiles and safety checks
#   make host-stage   build/stage$(HOST_PREFIX): bin, lib, share (no "stage":
#                     this is not for the AMIX sysroot)
#   make install      DESTDIR=... HOST_PREFIX=... (for packaging)
#   make screenshots  every step as PNG under a virtual X server (Xvfb);
#                     never opens a window on a real display
#   make run          start it from the source tree (on your own display)
COMPONENT_NAME    = amix-installer
COMPONENT_VERSION = 0.1.0

include ../../../make-rules/shared.mk

HOST_PREFIX ?= /usr/local
PYTHON      ?= python3
LOGO         = ../../../doc/assets/amix-logo.png
HOST_STAGE   = $(STAGE_DIR)$(HOST_PREFIX)
SOURCES      = $(wildcard amix_installer/*.py)

build: $(BUILD_DIR)/icons/.done
	$(PYTHON) -m py_compile $(SOURCES)

$(BUILD_DIR)/icons/.done: tools/mkicons.py $(LOGO)
	rm -rf $(BUILD_DIR)/icons
	$(PYTHON) tools/mkicons.py $(LOGO) $(BUILD_DIR)/icons
	touch $@

test:
	$(PYTHON) -m unittest discover -s tests -v

host-stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(HOST_STAGE)/bin $(HOST_STAGE)/lib/amix-installer/amix_installer \
		$(HOST_STAGE)/share/amix-installer $(HOST_STAGE)/share/applications
	cp amix-installer $(HOST_STAGE)/bin/
	cp $(SOURCES) $(HOST_STAGE)/lib/amix-installer/amix_installer/
	cp data/style.css data/style-dark.css $(LOGO) $(HOST_STAGE)/share/amix-installer/
	cp data/org.amix.Installer.desktop $(HOST_STAGE)/share/applications/
	cp -R $(BUILD_DIR)/icons $(HOST_STAGE)/share/
	rm -f $(HOST_STAGE)/share/icons/.done

install: host-stage
	mkdir -p $(DESTDIR)$(HOST_PREFIX)
	cp -R $(HOST_STAGE)/. $(DESTDIR)$(HOST_PREFIX)/

SHOT_DIR ?= $(BUILD_DIR)/shots
screenshots: build
	sh tools/screenshots.sh $(SHOT_DIR)

run: build
	./amix-installer

.PHONY: build test host-stage install screenshots run
