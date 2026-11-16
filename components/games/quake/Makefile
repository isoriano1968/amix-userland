# GLQuake and software Quake for AMIX, from id's GPL source; the game's data is your own.
COMPONENT_NAME    = quake
COMPONENT_VERSION = 1.09

include ../../../make-rules/shared.mk

build:
	$(MAKE) -f Makefile.amix
	$(MAKE) -f Makefile.amix-sw
	$(MAKE) -f Makefile.amix-zzgl

# on AMIX only GLQuake on core 1 (the manifest): /opt/quake; the game's data
# (/opt/quake/id1) is the user's own and not in the package
stage:
	$(MAKE) -f Makefile.amix-zzgl
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/opt/quake
	cp glquake.zzgl glquake.sh $(STAGE_DIR)/opt/quake/
	chmod 755 $(STAGE_DIR)/opt/quake/*
	@$(manifest_files)

.PHONY: build stage
