# cde-amixq3: Quake III / RtCW launcher for the Z3660's core 1 (amixq3), built from desktop/cde-apps' sources
# (cde-apps/build.sh, cde-apps/stage.sh amixq3); a package of its own for
# org.amix.hw.
COMPONENT_NAME    = cde-amixq3

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build:
	$(MAKE) -C ../cde-apps build

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	sh ../cde-apps/stage.sh $(STAGE_DIR) amixq3
	@$(manifest_files)
	$(CPU_SCAN) $(STAGE_DIR)

.PHONY: build stage
