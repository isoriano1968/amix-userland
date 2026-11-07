# C extension modules for the system Python 3.11 of AMIX (runtime/python3),
# cross-built with Meson: what pygame, numpy and the like share.  Meson asks
# a Python for the target's sysconfig; build/amixpy is the build machine's
# CPython 3.11 answering with AMIX's (_sysconfigdata__amigaunix_ and the
# Python headers from the sysroot, python-ext/sitecustomize.py.in).
# build/pkg-config is tools/amix-pkg-config without -lsocket and -lnsl
# (python-ext/pkg-config.in says why), build/native.ini names Cython.
#
#   HOST_PYTHON   a CPython 3.11 for the build machine (same minor version:
#                 Meson reads its sysconfig as AMIX's)
#   CYTHON        Cython 3 for the build machine, if a module has .pyx files
#   PYINC, SITE   the Python headers in the sysroot, site-packages on AMIX
#
# A recipe's build rule runs $(pyext_glue) after unpacking, then meson setup
# with --cross-file (its own, naming build/amixpy and build/pkg-config) and
# --native-file=$(CURDIR)/build/native.ini.  Examples:
# components/library/python/pygame, components/library/python/numpy.

PYEXT_FILES := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/python-ext
PYINC        = $(AMIX_SYSROOT)$(PREFIX)/include/python3.11
SITE         = $(PREFIX)/lib/python3.11/site-packages
HOST_PYTHON ?= python3.11
CYTHON      ?= cython

define pyext_glue
	mkdir -p build/pyamix
	cp $(AMIX_SYSROOT)$(PREFIX)/lib/python3.11/_sysconfigdata__amigaunix_.py build/pyamix/
	sed 's|@PYINC@|$(PYINC)|' $(PYEXT_FILES)/sitecustomize.py.in > build/pyamix/sitecustomize.py
	sed -e 's|@PYAMIX@|$(CURDIR)/build/pyamix|' -e 's|@HOST_PYTHON@|$(HOST_PYTHON)|' \
		$(PYEXT_FILES)/amixpy.in > build/amixpy
	chmod 755 build/amixpy
	sed 's|@PKG_CONFIG@|$(PKG_CONFIG)|' $(PYEXT_FILES)/pkg-config.in > build/pkg-config
	chmod 755 build/pkg-config
	printf "[binaries]\ncython = '%s'\n" "`command -v $(CYTHON)`" > build/native.ini
endef
