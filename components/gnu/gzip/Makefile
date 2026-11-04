# GNU gzip 1.14 for AMIX, in /usr/local/bin: gzip and its scripts (gunzip,
# zcat, zgrep, zdiff, ...); replaces the 1990s FSFgzip package.  In
# /usr/local, not /usr/gnu: AMIX has no gzip of its own to shadow, and tar's
# z and scripts look for gzip on the PATH.  The scripts use $(...), which
# AMIX's /bin/sh lacks: they run under /usr/gnu/bin/bash (shell/bash).
COMPONENT_NAME    = gzip
COMPONENT_VERSION = 1.14
COMPONENT_ARCHIVE = gzip-1.14.tar.xz
COMPONENT_URL     = https://ftp.gnu.org/gnu/gzip/gzip-1.14.tar.xz
COMPONENT_SHA256  = 01a7b881bd220bfdf615f97b8718f80bdfd3f6add385b993dcf6efd14e8c0ac6
CLEAN_FILES       = gzip-1.14 $(COMPONENT_ARCHIVE)
GNU_PREFIX        = /usr/local
PROGS             = gzip
EXTRA_CONF        =
COMPAT            =
SCRIPTS           = gunzip gzexe zcat zcmp zdiff zegrep zfgrep zforce zgrep zless zmore znew
# configure writes the build machine's shell into the first line: bash on AMIX;
# uncompress is gunzip under another name, as upstream's install makes it
STAGE_EXTRA       = for s in $(SCRIPTS); do \
	sed '1s|^\#!.*sh$$|\#!/usr/gnu/bin/bash|' build/$$s > $(STAGE_DIR)$(PREFIX)/bin/$$s && \
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/$$s || exit 1; done; \
	ln -sf gunzip $(STAGE_DIR)$(PREFIX)/bin/uncompress
include ../gnu.mk
