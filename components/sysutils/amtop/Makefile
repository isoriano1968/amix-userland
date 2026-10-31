# amtop: a small curses system monitor for AMIX.
COMPONENT_NAME    = amtop
COMPONENT_VERSION = 1

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)

build:
	mkdir -p $(BUILD_DIR)
	m68k-elf-gcc -O2 -m68060 -D__amix__ -D__svr4__ -DSVR4 -fcommon `$(PKG_CONFIG) --cflags ncurses` -c amtop.c -o $(BUILD_DIR)/amtop.o
	m68k-elf-gcc $(BUILD_DIR)/amtop.o `$(PKG_CONFIG) --libs ncurses` -o $(BUILD_DIR)/amtop

.PHONY: build
