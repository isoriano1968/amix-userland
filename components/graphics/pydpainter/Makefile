# PyDPainter 2.2.1 for AMIX: the Deluxe Paint clone in Python 3 + pygame, in
# /usr/local/share/pydpainter with the launcher /usr/local/bin/pydpainter.
# Pure Python; it needs library/python/pygame and library/python/numpy.
#
# A CDE action, icon and App Manager entry (Desktop_Apps), as graphics/grafx2.
# The AMIX front panel (components/desktop/cde-apps panel/amix.fp) is not
# touched here.  PyDPainter's own data stays beside it: it changes to its
# directory and loads data/, libs/ and gif2numpy/ from there.
#
# Patch 0001 (no tkinter, no SDL_mixer) tested on the PC with a Python that
# has no tkinter, NumPy 2.2.6 and pygame-ce 2.5.8, as on AMIX.  Not yet run
# on AMIX.
COMPONENT_NAME    = pydpainter
COMPONENT_VERSION = 2.2.1
COMPONENT_ARCHIVE = PyDPainter-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/mriale/PyDPainter/archive/refs/tags/v$(COMPONENT_VERSION).tar.gz
COMPONENT_SHA256  = 42f5a98b95d5521f83b095dac63139ab3bf64ff06ef055c5de6f8dc38c8c442f
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

S    = build/PyDPainter-$(COMPONENT_VERSION)
DATA = $(PREFIX)/share/pydpainter
DT   = $(STAGE_DIR)/etc/dt/appconfig

build: $(COMPONENT_ARCHIVE)
	rm -rf build
	mkdir -p build
	tar -C build -xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -d $(S) -p1 < $$p || exit 1; done

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(DATA) $(STAGE_DIR)$(PREFIX)/bin \
		$(DT)/types/C $(DT)/icons/C $(DT)/appmanager/C/Desktop_Apps
	cd $(S) && cp -R PyDPainter.py libs gif2numpy data $(STAGE_DIR)$(DATA)/
	rm -f $(STAGE_DIR)$(DATA)/data/Displayinfo.ods $(STAGE_DIR)$(DATA)/data/multiscale.py
	sed 's|@DATA@|$(DATA)|' files/pydpainter.in > $(STAGE_DIR)$(PREFIX)/bin/pydpainter
	cp files/pydpainter.dt $(DT)/types/C/
	python3 -I files/mkicons.py $(S)/data/logo.png PyDPainter $(DT)/icons/C
	: > $(DT)/appmanager/C/Desktop_Apps/PyDPainter
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/pydpainter $(DT)/appmanager/C/Desktop_Apps/PyDPainter
	chmod -R go-w,a+rX $(STAGE_DIR)
	@$(manifest_files)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar etc usr

.PHONY: build stage
