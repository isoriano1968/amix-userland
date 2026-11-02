# GNU bison 3.8.2 for AMIX, in /usr/gnu/bin.
COMPONENT_NAME    = bison
COMPONENT_VERSION = 3.8.2
COMPONENT_ARCHIVE = bison-3.8.2.tar.xz
COMPONENT_URL     = https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.xz
COMPONENT_SHA256  = 9bba0214ccf7f1079c5d59210045227bcf619519840ebfa80cd3849cff5a5bf2
CLEAN_FILES       = bison-3.8.2 $(COMPONENT_ARCHIVE)
PROGS             = src/bison
EXTRA_CONF        = --disable-threads --disable-yacc
EXTRA_CFLAGS      = -DAMIX_RUSAGE
STAGE_EXTRA       = mkdir -p $(STAGE_DIR)$(PREFIX)/share/bison && cp -R $(S)/data/. $(STAGE_DIR)$(PREFIX)/share/bison/
include ../gnu.mk
