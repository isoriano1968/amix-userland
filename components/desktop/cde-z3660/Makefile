# cde-z3660: CDE for the Z3660, built from desktop/cde-apps' sources
# (cde-apps/build.sh, cde-apps/stage.sh z3660); a package of its own for
# org.amix.hw.
COMPONENT_NAME    = cde-z3660

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build:
	$(MAKE) -C ../cde-apps build

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	sh ../cde-apps/stage.sh $(STAGE_DIR) z3660
	@$(manifest_files)
	$(CPU_SCAN) $(STAGE_DIR)

.PHONY: build stage
