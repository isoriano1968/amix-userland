# CDE applications for AMIX: AmixCast, ztopcde, the AMIX front panel control.
COMPONENT_NAME    = cde-apps

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build:
	sh build.sh

# what the install.sh scripts put on the box, for the IPS package (stage.sh)
stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	sh stage.sh $(STAGE_DIR)
	@$(manifest_files)
	$(CPU_SCAN) $(STAGE_DIR)

.PHONY: build stage
