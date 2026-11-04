# Components whose upstream builds with CMake.  CMake runs in build/cmake with
# make-rules/amix-toolchain.cmake (the compiler, and the sysroot as the only
# place for headers and libraries).  The recipe sets:
#
#   CMAKE_OPTIONS    -D... for the project, added to the defaults below
#   CMAKE_CFLAGS     CMAKE_C_FLAGS (the toolchain file adds the AMIX defines)
#   CMAKE_SOURCE     default $(S): the directory with CMakeLists.txt
#   BUILD_POST       extra build commands, after the build, before $(mkshlibs)
#   SHLIBS ...       shared libraries (shlib.mk)
#
#   make build   unpack, patch, cmake, build, shared libraries
#   make stage   cmake's install into build/stage, finish (shlib.mk), the tar
#
# Example: components/library/libupnp.

RULES := $(dir $(lastword $(MAKEFILE_LIST)))
include $(RULES)unpack.mk
include $(RULES)shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

CMAKE_SOURCE ?= $(S)
CMAKE_CFLAGS ?= -O2 -fPIC
CMAKE_DEFAULTS = -DCMAKE_TOOLCHAIN_FILE=$(abspath $(RULES)amix-toolchain.cmake) \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_C_FLAGS="$(CMAKE_CFLAGS)"

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	mkdir -p build/cmake
	cd build/cmake && cmake ../../$(CMAKE_SOURCE) $(CMAKE_DEFAULTS) $(CMAKE_OPTIONS) \
		> ../cmake.log 2>&1 || { tail -20 ../cmake.log; exit 1; }
	$(MAKE) -C build/cmake -j8 > build/make.log 2>&1 || \
		{ grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	$(BUILD_POST)
	$(mkshlibs)

stage:
	rm -rf $(STAGE_DIR)
	$(MAKE) -C build/cmake install DESTDIR=$(STAGE_DIR) > build/install.log 2>&1 || \
		{ tail -20 build/install.log; exit 1; }
	$(stage_finish)

.PHONY: build stage
