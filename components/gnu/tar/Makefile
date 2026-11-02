# GNU tar 1.35 for AMIX, in /usr/gnu/bin.
COMPONENT_NAME    = tar
COMPONENT_VERSION = 1.35
COMPONENT_ARCHIVE = tar-1.35.tar.xz
COMPONENT_URL     = https://ftp.gnu.org/gnu/tar/tar-1.35.tar.xz
COMPONENT_SHA256  = 4d62ff37342ec7aed748535323930c7cf94acf71c3591882b26a7ea50f3edc16
CLEAN_FILES       = tar-1.35 $(COMPONENT_ARCHIVE)
PROGS             = src/tar
EXTRA_CONF        = --disable-acl --without-selinux --without-xattrs
COMPAT            = glob
include ../gnu.mk
