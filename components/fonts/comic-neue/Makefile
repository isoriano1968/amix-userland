# Comic Neue 2.51 for AMIX: the TrueType files, in
# /usr/local/share/fonts/comic-neue (NetSurf's cursive face; SIL OFL).
COMPONENT_NAME    = comic-neue
COMPONENT_VERSION = 2.51
COMPONENT_ARCHIVE = comicneue-$(COMPONENT_VERSION).zip
COMPONENT_URL     = https://github.com/crozynski/comicneue/releases/download/$(COMPONENT_VERSION)/comicneue-master.zip
COMPONENT_SHA256  = 746036c97f2e73012734ef9630c0f0253abfe45f038f16119e74497270945740
CLEAN_FILES       = comicneue-master $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

S = comicneue-master
FONTDIR = $(PREFIX)/share/fonts/comic-neue

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	unzip -q $(COMPONENT_ARCHIVE)
	mkdir -p build

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(FONTDIR)
	cp $(S)/Fonts/TTF/ComicNeue/*.ttf $(S)/OFL.txt $(STAGE_DIR)$(FONTDIR)/
	chmod 755 $(STAGE_DIR)$(FONTDIR); chmod 644 $(STAGE_DIR)$(FONTDIR)/*
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/comic-neue-amix.tar usr

.PHONY: build stage
