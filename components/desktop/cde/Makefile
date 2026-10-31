# Motif 2.3.8 and CDE 2.2.4a for AMIX.
COMPONENT_NAME    = cde
COMPONENT_VERSION = 2.2.4a

include ../../../make-rules/shared.mk

# config.sh from config.sh.example: your paths
build: config.sh
	sh fetch.sh
	./build.sh

.PHONY: build
