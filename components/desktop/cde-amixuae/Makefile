# cde-amixuae: Amiga, built from desktop/cde-apps' sources
# (cde-apps/build.sh, cde-apps/stage.sh amixuae); a package of its own for
# org.amix.hw.
COMPONENT_NAME    = cde-amixuae

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build:
	$(MAKE) -C ../cde-apps build

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	sh ../cde-apps/stage.sh $(STAGE_DIR) amixuae
	@$(manifest_files)
	$(CPU_SCAN) $(STAGE_DIR)

.PHONY: build stage
