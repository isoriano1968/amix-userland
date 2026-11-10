# QuickJS-ng 0.18.0 for AMIX: a modern JavaScript engine (ES2023 and later:
# classes, async/await, modules, BigInt, Proxy, WeakRef ...), as
# libqjs.so.0 with quickjs.h and quickjs-libc.h (the std and os modules
# built in, as upstream's QJS_BUILD_LIBC), and qjs and qjsc.  Meant first
# for NetSurf's JavaScript in place of Duktape (ES5.1).
#
# Upstream builds with CMake or Meson; the library is only five C files,
# compiled here directly.  gcc 14 only (C11).
#
# Patches: 0001 the AMIX platform (no threads or atomics, as DJGPP; the POSIX
# calls SVR4.0 lacks, in quickjs-libc), 0002 big-endian machines compile the
# built-ins kept as little-endian bytecode (Array.fromAsync, Iterator.zip)
# from their JavaScript source (files/js2c.py makes the C strings).
#
# Maths: files/amix-ieee-math.h is forced into every source.  AMIX's own C89
# maths is not IEEE 754 (floor(NaN) sends SIGFPE through modf, fmod(x, 0)
# returns x, "DOMAIN error" from matherr), so floor, fmod, pow, sin ... go to
# musl's long double functions in library/libm99 (files/amix-ieee-math.c).
#
# qjs: the REPL and standalone executables (-c) are bytecode from a
# little-endian qjsc too (gen/repl.c, gen/standalone.c); qjs here has empty
# ones (files/qjs-nobytecode.c): qjs FILE and -e work, the REPL does not.
#
#   make build stage   library, qjs, qjsc
#   make test-amix     build/qjstest.tar: qjs and upstream's tests/ with
#                      files/run-tests.sh, to run on the Amiga
COMPONENT_NAME    = quickjs-ng
COMPONENT_VERSION = 0.18.0
COMPONENT_ARCHIVE = quickjs-ng-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/quickjs-ng/quickjs/archive/refs/tags/v$(COMPONENT_VERSION).tar.gz
COMPONENT_SHA256  = 28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd
S                 = quickjs-$(COMPONENT_VERSION)

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

CC68    = m68k-elf-gcc
M99     = $(shell $(PKG_CONFIG) --cflags libm99)
CFLAGS  = -O2 -m68060 -std=gnu11 -D__amix__ -DSVR4 -D_GNU_SOURCE -DQJS_BUILD_LIBC \
	-fno-split-wide-types -Wno-error=incompatible-pointer-types \
	-include $(CURDIR)/files/amix-ieee-math.h $(M99)
LIBSRCS = quickjs libregexp libunicode dtoa quickjs-libc
LIBDEPS = -L$(AMIX_SYSROOT)/usr/local/lib -L$(AMIX_SYSROOT)/usr/ccs/lib -lm99 -lamixc99 \
	-ldl -lsocket -lnsl -lgen -lm
BUILTINS = array-fromasync iterator-zip iterator-zip-keyed

SHLIBS                    = libqjs.so.0
SHLIB_ARCHIVE_libqjs.so.0 = build/libqjs_pic.a
SHLIB_ARGS_libqjs.so.0    = $(LIBDEPS)
SHLIB_STUB_ONLY_libqjs.so.0 = ^(JS_|js_|JS)

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	mkdir -p build/pic build/obj
	for b in $(BUILTINS); do python3 -I files/js2c.py qjsc_builtin_`echo $$b | tr - _` \
		$(S)/builtin-$$b.js $(S)/builtin-$$b.src.h || exit 1; done
	for f in $(LIBSRCS); do $(CC68) $(CFLAGS) -fPIC -c $(S)/$$f.c -o build/pic/$$f.o || exit 1; done
	$(CC68) -O2 -m68060 -fPIC -c files/amix-ieee-math.c -o build/pic/amix-ieee-math.o
	$(TOOLS)/amix-got-check build/pic/*.o
	# 060 traps: movep only, gcc's switch tables in .text read as code
	# (the scan's documented false hit); anything else stops the build
	$(TOOLS)/amix-fpu-scan -q build/pic/*.o | awk '$$2 !~ /^movep[wl]$$/ { print; bad = 1 } END { exit bad }'
	m68k-elf-ar rcs build/libqjs_pic.a build/pic/*.o
	$(mkshlibs)
	$(CC68) $(CFLAGS) -c $(S)/qjs.c -o build/obj/qjs.o
	$(CC68) $(CFLAGS) -c $(S)/qjsc.c -o build/obj/qjsc.o
	$(CC68) -O2 -m68060 -c files/qjs-nobytecode.c -o build/obj/qjs-nobytecode.o
	$(CC68) -m68060 -o build/qjs build/obj/qjs.o build/obj/qjs-nobytecode.o -Lbuild -lqjs \
		-Wl,-rpath-link,$(AMIX_SYSROOT)/usr/lib -Wl,-R,$(PREFIX)/lib $(LIBDEPS)
	$(CC68) -m68060 -o build/qjsc build/obj/qjsc.o -Lbuild -lqjs \
		-Wl,-rpath-link,$(AMIX_SYSROOT)/usr/lib -Wl,-R,$(PREFIX)/lib $(LIBDEPS)
	m68k-elf-strip build/qjs build/qjsc
	$(TOOLS)/amix-fix-needed build/qjs > /dev/null
	$(TOOLS)/amix-fix-needed build/qjsc > /dev/null

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/include $(STAGE_DIR)$(PREFIX)/lib/pkgconfig \
		$(STAGE_DIR)$(PREFIX)/share/man/man1 $(STAGE_DIR)$(PREFIX)/share/doc/quickjs-ng
	cp build/qjs build/qjsc $(STAGE_DIR)$(PREFIX)/bin/
	cp $(S)/quickjs.h $(S)/quickjs-libc.h $(STAGE_DIR)$(PREFIX)/include/
	cp $(S)/qjs.man $(STAGE_DIR)$(PREFIX)/share/man/man1/qjs.1
	cp $(S)/qjsc.man $(STAGE_DIR)$(PREFIX)/share/man/man1/qjsc.1
	cp $(S)/LICENSE $(STAGE_DIR)$(PREFIX)/share/doc/quickjs-ng/
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' files/qjs.pc.in \
		> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/qjs.pc
	$(stage_finish)
	$(manifest_files)

test-amix:
	rm -rf build/qjstest && mkdir -p build/qjstest
	cp build/qjs build/libqjs.so.0 files/run-tests.sh build/qjstest/
	cp -R $(S)/tests build/qjstest/tests
	tar -C build -cf build/qjstest.tar qjstest

.PHONY: build stage test-amix
