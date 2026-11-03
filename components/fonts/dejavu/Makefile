# DejaVu fonts 2.37 for AMIX: the TrueType files, in
# /usr/local/share/fonts/dejavu (NetSurf's FreeType fonts).
COMPONENT_NAME    = dejavu
COMPONENT_VERSION = 2.37
COMPONENT_ARCHIVE = dejavu-fonts-ttf-$(COMPONENT_VERSION).tar.bz2
COMPONENT_URL     = https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
CLEAN_FILES       = dejavu-fonts-ttf-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

S = dejavu-fonts-ttf-$(COMPONENT_VERSION)
FONTDIR = $(PREFIX)/share/fonts/dejavu

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xjf $(COMPONENT_ARCHIVE)
	mkdir -p build

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(FONTDIR)
	cp $(S)/ttf/*.ttf $(S)/LICENSE $(STAGE_DIR)$(FONTDIR)/
	chmod 755 $(STAGE_DIR)$(FONTDIR); chmod 644 $(STAGE_DIR)$(FONTDIR)/*
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/dejavu-amix.tar usr

.PHONY: build stage
