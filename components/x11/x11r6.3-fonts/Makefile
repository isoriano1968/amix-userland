# The X11R6.3 core fonts for AMIX's X server Xamix: misc (with "fixed" and
# the cursor font), 75dpi and 100dpi as PCF, and the scalable Speedo and
# Type1 fonts, in /usr/x11r6/lib/X11/fonts: the font path Xamix has built
# in, and the one desktop/cde-dtlogin's Xamix-dt gives it.  From the pristine
# X11R6.3 archive xc-2.tar.gz (the one x11/x11r6.3 fetches and checks),
# built on the host: bdftopcf for each BDF, mkfontdir for fonts.dir (and
# fonts.scale for the scalable ones), the fonts.alias of the sources.
#
# Until 2026-11-20 X used stock AMIX's X11R5 fonts (/usr/X/lib/fonts, from
# Commodore's Xcore package); an AMIX without Xcore had no font at all and
# dtwm stopped with "failed to load font".
COMPONENT_NAME    = x11r6.3-fonts
COMPONENT_VERSION = 6.3
XC2               = ../x11r6.3/distfiles/xc-2.tar.gz
XC2_SHA256        = cb9e6725a6f10b345c3dea417821d79a0738c09da0a16fe6b14b2ade878dcb8d

include ../../../make-rules/shared.mk

FONTS  = /usr/x11r6/lib/X11/fonts
BITMAP = misc 75dpi 100dpi
SCALED = Speedo Type1
SRC    = $(BUILD_DIR)/xc/fonts

$(XC2):
	$(MAKE) -C ../x11r6.3 $(abspath $(XC2))

build: $(XC2)
	echo "$(XC2_SHA256)  $(XC2)" | sha256sum -c --quiet -
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/pcf
	gzip -dc $(XC2) | tar -C $(BUILD_DIR) -xf - xc/fonts/bdf xc/fonts/scaled
	# PCF in the sources' own way: bdftopcf as X11R6.3's MakeFonts runs it;
	# the server's reader takes any byte and bit order
	for d in $(BITMAP); do \
		mkdir -p $(BUILD_DIR)/pcf/$$d; \
		for f in $(SRC)/bdf/$$d/*.bdf; do \
			bdftopcf -t -o $(BUILD_DIR)/pcf/$$d/`basename $$f .bdf`.pcf $$f || exit 1; \
		done; \
		cp $(SRC)/bdf/$$d/fonts.alias $(BUILD_DIR)/pcf/$$d/; \
		(cd $(BUILD_DIR)/pcf/$$d && mkfontdir .) || exit 1; \
	done

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(FONTS)
	for d in $(BITMAP); do \
		mkdir -p $(STAGE_DIR)$(FONTS)/$$d && \
		cp $(BUILD_DIR)/pcf/$$d/*.pcf $(BUILD_DIR)/pcf/$$d/fonts.dir $(BUILD_DIR)/pcf/$$d/fonts.alias \
			$(STAGE_DIR)$(FONTS)/$$d/ || exit 1; \
	done
	# the scalable ones as X11R6.3 installs them: the outlines, fonts.scale,
	# and fonts.dir made from fonts.scale (R6.3's mkfontdir copies it)
	for d in $(SCALED); do \
		mkdir -p $(STAGE_DIR)$(FONTS)/$$d && \
		(cd $(SRC)/scaled/$$d && cp `ls | grep -v -e Imakefile -e '^COPYRIGHT' -e '^Copyright'` \
			$(STAGE_DIR)$(FONTS)/$$d/) && \
		cp $(STAGE_DIR)$(FONTS)/$$d/fonts.scale $(STAGE_DIR)$(FONTS)/$$d/fonts.dir || exit 1; \
	done
	chmod -R u+w,go-w,a+rX $(STAGE_DIR)
	find $(STAGE_DIR) -type f -exec chmod 644 {} +
	@$(manifest_files)
	@test "`cd $(STAGE_DIR) && find . -type f -o -type l | wc -l`" = "`grep -c '^file ' x11r6.3-fonts.manifest`" || \
		{ echo "the stage and x11r6.3-fonts.manifest list different files"; exit 1; }
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
