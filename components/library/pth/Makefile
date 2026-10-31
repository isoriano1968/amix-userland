# GNU Pth for AMIX: cooperative threads and a pthread library.
COMPONENT_NAME    = pth
COMPONENT_VERSION = 2.0.7
COMPONENT_ARCHIVE = pth-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://ftp.gnu.org/gnu/pth/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 72353660c5a2caafd601b20e12e75d865fd88f6cf1a088b306a3963f0bc77232

include ../../../make-rules/shared.mk

# thread switches without system calls (see tools/build-gnu-pth.sh)
export PTH_MCTX   = --with-mctx-mth=sjlj --with-mctx-dsp=usjlj --with-mctx-stk=sas
export PTH_SOURCE = $(BUILD_DIR)/pth-$(COMPONENT_VERSION)
export PTH_PREFIX = $(STAGE_DIR)

build:
	mkdir -p $(BUILD_DIR)
	CC=$(AMIX_CC) bash tools/build-gnu-pth.sh

.PHONY: build
