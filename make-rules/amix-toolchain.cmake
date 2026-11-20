# CMake toolchain for AMIX (m68k SVR4, gcc 14 cross): cmake
# -DCMAKE_TOOLCHAIN_FILE=.../make-rules/amix-toolchain.cmake.  Libraries and
# headers only from the sysroot (AMIX_SYSROOT), programs from the build
# machine; pkg-config is tools/amix-pkg-config.
set(CMAKE_SYSTEM_NAME UNIX_System_V)
set(CMAKE_SYSTEM_VERSION 4)
set(CMAKE_SYSTEM_PROCESSOR m68k)

get_filename_component(_amix_rules "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
if(DEFINED ENV{AMIX_TOOLS})
	set(_amix_tools "$ENV{AMIX_TOOLS}")
else()
	set(_amix_tools "$ENV{HOME}/opt/m68k-amix/bin")
endif()
if(DEFINED ENV{AMIX_SYSROOT})
	set(_amix_sysroot "$ENV{AMIX_SYSROOT}")
else()
	set(_amix_sysroot "$ENV{HOME}/opt/m68k-amix-sysroot")
endif()

set(CMAKE_C_COMPILER "${_amix_tools}/m68k-elf-gcc")
set(CMAKE_CXX_COMPILER "${_amix_tools}/m68k-elf-g++")
set(CMAKE_AR "${_amix_tools}/m68k-elf-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_amix_tools}/m68k-elf-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${_amix_tools}/m68k-elf-strip" CACHE FILEPATH "")
set(PKG_CONFIG_EXECUTABLE "${_amix_rules}/../tools/amix-pkg-config" CACHE FILEPATH "")

# the CPU: AMIX_CPU from the environment, else the AMIX baseline
# (doc/CPU-TARGET-68020-60.md; make-rules/shared.mk's AMIX_CPU_BASELINE)
if(DEFINED ENV{AMIX_CPU} AND NOT "$ENV{AMIX_CPU}" STREQUAL "")
	set(_amix_cpu "$ENV{AMIX_CPU}")
else()
	set(_amix_cpu "-mcpu=68020 -mtune=68060 -fexcess-precision=fast")
endif()

# what every AMIX build defines: not Linux, SVR4, and AMIX itself
set(CMAKE_C_FLAGS_INIT "${_amix_cpu} -fcommon -U__linux__ -Ulinux -D__amix__ -DSVR4")
set(CMAKE_CXX_FLAGS_INIT "${_amix_cpu} -U__linux__ -Ulinux -D__amix__ -DSVR4")

set(CMAKE_FIND_ROOT_PATH "${_amix_sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
