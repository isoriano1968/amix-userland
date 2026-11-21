# xtinherit.mk -- a program's own libXtInherit.a, as desktop/cde and
# desktop/cde-apps make it: xm.pc and xt.pc name -lXtInherit, which the
# x11r6.3 package does not ship (on AMIX the Amiga's own gcc makes it;
# x11r6.3's files/xtinherit.c says why).  A recipe runs $(XTINHERIT_BUILD)
# after it cleans build/ and links with $(XTINHERIT_LIBS) before the X libs.
XTINHERIT_SRC := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../components/x11/x11r6.3/files/xtinherit.c)
XTINHERIT_DIR = $(CURDIR)/build/xtinherit
XTINHERIT_LIBS = -L$(XTINHERIT_DIR)
XTINHERIT_BUILD = mkdir -p $(XTINHERIT_DIR) && \
	m68k-elf-gcc -O2 $(AMIX_CPU_BASELINE) -I$(AMIX_SYSROOT)/usr/x11r6/include -D__amix__ -DSVR4 -w \
		-c $(XTINHERIT_SRC) -o $(XTINHERIT_DIR)/xtinherit.o && \
	m68k-elf-ar rcsD $(XTINHERIT_DIR)/libXtInherit.a $(XTINHERIT_DIR)/xtinherit.o && \
	rm -f $(XTINHERIT_DIR)/xtinherit.o
