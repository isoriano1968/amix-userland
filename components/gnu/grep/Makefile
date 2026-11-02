# GNU grep 3.11 for AMIX, in /usr/gnu/bin.
COMPONENT_NAME    = grep
COMPONENT_VERSION = 3.11
COMPONENT_ARCHIVE = grep-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://ftp.gnu.org/gnu/grep/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 1db2aedde89d0dea42b16d9528f894c8d15dae4e190b59aecc78f5a951276eab
CLEAN_FILES       = grep-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)
PROGS             = src/grep
EXTRA_CONF        = --disable-threads --disable-perl-regexp
include ../gnu.mk
