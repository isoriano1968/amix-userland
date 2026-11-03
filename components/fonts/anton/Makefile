# Anton for AMIX: the TrueType file, in /usr/local/share/fonts/anton
# (NetSurf's fantasy face, in the style of Impact; SIL OFL).  Upstream has no
# releases: the source is the commit of 2024-04-04.
COMPONENT_NAME    = anton
COMPONENT_VERSION = 2.116
ANTON_COMMIT      = beb92fcad87808357123bb66881b4032dc96efe7
COMPONENT_ARCHIVE = AntonFont-$(ANTON_COMMIT).tar.gz
COMPONENT_URL     = https://github.com/googlefonts/AntonFont/archive/$(ANTON_COMMIT).tar.gz
COMPONENT_SHA256  = 4c943e95475d90f995e88e27642ee26a3d2a79a03fb120fdbe3048452c5af26b
CLEAN_FILES       = AntonFont-$(ANTON_COMMIT) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

S = AntonFont-$(ANTON_COMMIT)
FONTDIR = $(PREFIX)/share/fonts/anton

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	mkdir -p build

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(FONTDIR)
	cp $(S)/fonts/Anton-Regular.ttf $(S)/OFL.txt $(STAGE_DIR)$(FONTDIR)/
	chmod 755 $(STAGE_DIR)$(FONTDIR); chmod 644 $(STAGE_DIR)$(FONTDIR)/*
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/anton-amix.tar usr

.PHONY: build stage
