# NumPy for the system Python 3.11 of AMIX (runtime/python3): the C and C++
# modules cross-built with gcc 14 (-m68060) by numpy's own (vendored) Meson,
# without BLAS/LAPACK (numpy.linalg on its bundled lapack-lite), without SIMD
# dispatch (m68k has none), installed into /usr/local/lib/python3.11/site-packages.
# 2.2.6: the last release of the 2.2 series, which PyDPainter 2.2.1 pins
# (requirements.txt numpy==2.2.2); 2.2 supports Python 3.10 to 3.13.
#
# The maths: AMIX's libm is static, C89 doubles only.  numpy's C99 functions
# (float and long double variants, expm1, log2, complex.h ...) come from
# library/libm99 (libm99.so.1, NEEDED, runpath /usr/local/lib); what libm99
# leaves to AMIX's libm is linked from lib060's libm060.a first (sin, cos,
# tan, fmod, sqrt, atan, atan2, hypot, pow without the 060-trapping FSIN,
# FMOVECR ...), then libm.a.  Patch 0001 lets the cross file name them
# (property libm_link_args).  Long doubles are the 68881's 96-bit extended
# format (property longdouble_format: numpy knows it as
# MOTOROLA_EXTENDED_12_BYTES_BE, but can only find it by running a program).
#
# build/amix-gcc and build/amix-g++ (files/amix-cc.in) link every module
# with -Wl,--exclude-libs,ALL (the static archives stay local to each
# module), and the C++ modules (_multiarray_umath, _pocketfft_umath ...)
# with files/so-init around them and libstdc++ inside: AMIX's ld.so.1 runs
# only DT_INIT, not .init_array, and gcc's -shared link registers no
# exception frames.  The cross libstdc++ is static and non-PIC: those modules
# carry text relocations, which ld.so.1 accepts.
#
# files/amix/amix-numpy.h, forced into every file: llabs, wmemchr (SVR4.0's
# libc has neither), the C99 classification functions in std:: (the cross
# libstdc++ was configured without C99 maths), Python's AMIX declarations
# with C linkage in C++.  files/amix/amix-math.c, linked into every module:
# rint and modff (AMIX has them only in libc.a, which a module cannot take).
# intptr_t is made int (-D__INTPTR_TYPE__=int) as ssize_t is on AMIX: numpy mixes Py_intptr_t and npy_intp (= Py_ssize_t)
# pointers, gcc 14's m68k-elf says long for intptr_t.
# Patches: 0002 strtoll/strtoull (not in SVR4.0's libc), 0003 no TLS
# (ld.so.1 has no TLS relocations; numpy's thread-local caches become
# static, under the GIL and Pth), 0004 a ptrdiff_t/npy_intp mix-up in
# pocketfft (upstream assumes they are one type), 0005 np.finfo for the
# 68881's long double (numpy knew only the x87's; without it np.finfo warns
# and probes).
#
# Not installed, for the A3000's small root disk: the tests (every tests/
# directory; meson leaves out the test modules by tag), numpy.distutils and
# numpy.f2py (STAGE_DROP: build tools for C and Fortran extensions, which
# AMIX cannot compile natively: no gcc 14, no Fortran), and the .pyi typing
# stubs (make stage KEEP_PYI=1 keeps them).  The headers and npymath
# (numpy/_core/include, numpy/_core/lib/libnpymath.a, npymath.ini) are
# installed: they are what another extension module builds against.
#
#   make build    unpack, patch, Meson + ninja, the checks below
#   make stage    meson install (runtime) into build/stage, build/numpy-amix.tar
#   make check    tools/amix-pyext-check on every module: undefined symbols
#                 against python3 + libc + libm99, constructors, TLS, and
#                 060-trapping instructions (amix-fpu-scan without switch tables)
#   OPT=-Os       smaller modules (16.7 MB installed instead of 18.6 MB)
#
# tests/numpy-smoke.py [--pygame]: NumPy as PyDPainter uses it, a digest per
# check; tests/numpy-smoke.expected is the PC's output (NumPy 2.2.6 wheel,
# pygame-ce 2.5.8, CPython 3.11.18), to diff against the box's.
#
# Build machine: HOST_PYTHON (CPython 3.11) and CYTHON (Cython >= 3.0.6).
# The sysroot needs runtime/python3 (headers, _sysconfigdata, the python3.11
# program for the check), library/libm99 and library/lib060.
COMPONENT_NAME    = numpy
COMPONENT_VERSION = 2.2.6
COMPONENT_ARCHIVE = numpy-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://files.pythonhosted.org/packages/76/21/7d2a95e4bba9dc13d043ee156a356c0a8f0c6309dff6b21b4d71a073b8a8/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = e29554e2bef54a90aa5cc07da6ce955accb83f21ab5de01a62c8478897b264fd
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../../make-rules/shared.mk
include ../../../../make-rules/python-ext.mk

export PATH := $(AMIX_TOOLS):$(PATH)
TOOLS   = $(abspath ../../../../tools)
S       = build/numpy-$(COMPONENT_VERSION)
MESON   = $(HOST_PYTHON) $(CURDIR)/$(S)/vendored-meson/meson/meson.py
SOI     = $(CURDIR)/build/so-init
LIBDIR  = $(AMIX_SYSROOT)$(PREFIX)/lib
M99     = $(shell $(PKG_CONFIG) --cflags libm99)
LIBM    = $(CURDIR)/build/libamixmath.a $(shell $(PKG_CONFIG) --libs libm99) -L$(LIBDIR) -lm060 -lm
AMIXH   = -include $(PYINC)/amix-python.h
NPH     = -include $(CURDIR)/files/amix/amix-numpy.h
mlist   = [$(foreach a,$(1),'$(a)',)]
STAGE_DROP ?= distutils f2py
OPT        ?= -O2

COMMON  = -m68060 -fPIC -U__linux__ -Ulinux -D__amix__ -DSVR4 $(OPT) \
	-U__INTPTR_TYPE__ -D__INTPTR_TYPE__=int -U__UINTPTR_TYPE__ -D__UINTPTR_TYPE__=unsigned_int \
	$(M99) -I$(PYINC) -I$(PYINC)/amixcompat -I$(AMIX_SYSROOT)/usr/local/gnu/include

define numpy_cross
[binaries]
c = '$(CURDIR)/build/amix-gcc'
cpp = '$(CURDIR)/build/amix-g++'
ar = '$(AMIX_TOOLS)/m68k-elf-ar'
strip = '$(AMIX_TOOLS)/m68k-elf-strip'
pkg-config = '$(CURDIR)/build/pkg-config'
python = '$(CURDIR)/build/amixpy'

[built-in options]
c_args = $(call mlist,$(COMMON) -fcommon $(AMIXH) $(NPH))
cpp_args = $(call mlist,$(COMMON) $(NPH))

[properties]
sys_root = '$(AMIX_SYSROOT)'
longdouble_format = 'MOTOROLA_EXTENDED_12_BYTES_BE'
libm_link_args = $(call mlist,$(LIBM))

[host_machine]
system = 'amix'
cpu_family = 'm68k'
cpu = '68060'
endian = 'big'
endef
export numpy_cross

build: $(COMPONENT_ARCHIVE)
	rm -rf build
	mkdir -p build/so-init
	tar -C build -xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -d $(S) -p1 < $$p || exit 1; done
	$(pyext_glue)
	m68k-elf-gcc -c files/so-init/begin.s -o $(SOI)/begin.o
	m68k-elf-gcc -c files/so-init/end.s -o $(SOI)/end.o
	m68k-elf-gcc -m68060 -O2 -fPIC -c files/so-init/init.c -o $(SOI)/init.o
	m68k-elf-gcc -m68060 -O2 -fPIC -c files/amix/amix-math.c -o build/amix-math.o
	$(TOOLS)/amix-got-check $(SOI)/init.o build/amix-math.o
	m68k-elf-ar rc build/libamixmath.a build/amix-math.o
	for c in gcc g++; do sed -e "s|@CC@|$(AMIX_TOOLS)/m68k-elf-$$c|g" \
		-e 's|@SO_BEGIN@|$(SOI)/begin.o $(SOI)/init.o|' -e 's|@SO_END@|$(SOI)/end.o|' \
		-e 's|@LIBM@|$(LIBM)|' files/amix-cc.in > build/amix-$$c && chmod 755 build/amix-$$c || exit 1; done
	printf '%s\n' "$$numpy_cross" | sed 's/unsigned_int/unsigned int/' > build/amix-cross.ini
	$(MESON) setup build/meson $(S) --cross-file=$(CURDIR)/build/amix-cross.ini \
		--native-file=$(CURDIR)/build/native.ini --prefix=$(PREFIX) --buildtype=plain \
		-Dc_std=gnu11 -Dcpp_std=gnu++17 -Db_ndebug=true \
		-Dallow-noblas=true -Dblas=none -Dlapack=none \
		-Ddisable-optimization=true -Dcpu-baseline=none -Dcpu-dispatch=none \
		> build/meson.log 2>&1 || { tail -20 build/meson.log; exit 1; }
	ninja -C build/meson > build/make.log 2>&1 || \
		{ grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	find build/meson -name '*.o' | xargs $(TOOLS)/amix-got-check
	$(MAKE) check

check:
	$(TOOLS)/amix-pyext-check `find build/meson -name '*.cpython-311.so'`

stage:
	rm -rf $(STAGE_DIR)
	DESTDIR=$(STAGE_DIR) $(MESON) install -C build/meson --no-rebuild --tags=runtime,python-runtime,devel \
		> build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	find $(STAGE_DIR) -depth -type d -name tests -exec rm -rf {} +
	cd $(STAGE_DIR)$(SITE)/numpy && rm -rf $(STAGE_DROP)
	$(if $(KEEP_PYI),,find $(STAGE_DIR) -name '*.pyi' -exec rm -f {} +)
	for m in `find $(STAGE_DIR) -name '*.so'`; do m68k-elf-strip $$m && \
		$(TOOLS)/amix-fix-needed $$m > /dev/null || exit 1; done
	$(TOOLS)/amix-pyext-check `find $(STAGE_DIR) -name '*.so'`
	chmod -R go-w,a+rX $(STAGE_DIR)
	@$(manifest_files)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage check
