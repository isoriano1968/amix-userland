# uae4arm for the Z3660's ARM core 1: bare metal (newlib), Cortex-A9 + NEON.
#
#   make SRC=<patched uae4arm tree> TARGET=host   -> build/host/uaehost.elf
#        (semihosting: runs under qemu-arm -cpu cortex-a9 on the PC, files
#        through the host; for functional tests and PPM/WAV output)
#   make SRC=<tree> TARGET=core1 Z3660=<firmware> -> build/core1/z3660ARM_Core_uae.bin
#        (the image armbench loads; files, input, picture and sound through
#        the rings in c1uae.h)
#
# The emulator's sources and their list come from upstream's Makefile.common;
# what is ours lives next to this file.
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

SRC     ?= $(error SRC= the patched uae4arm tree)
TARGET  ?= host
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OUT     ?= $(CURDIR)/build/$(TARGET)

CORE_DIR := $(SRC)
HAVE_NEON := 1
include $(SRC)/Makefile.common

# Cortex-A9: ARMv7-A with NEON, but no integer divide instruction (SDIV/UDIV
# are optional in v7-A and the A9 has none): no ARM_HAS_DIV, the JIT must
# call the library for DIVU/DIVS
CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
DEFS    := -D__LIBRETRO__ -DARMV6_ASSEMBLY -DARMV6T2 -DPANDORA -DRASPBERRY -DUSE_ARMNEON \
           -DINLINE=inline -DAMIX_C1 -DAMIX_C1_$(TARGET)
OPT     ?= -O3 -fomit-frame-pointer
EXTRA   ?=
# int32_t as int (newlib says long; uae's own types.h and sysdeps.h
# disagree otherwise), as the GL cores do
INT32   := -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ -D__UINT32_TYPE__="unsigned int" \
           -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int -U__UINT_LEAST32_TYPE__ \
           -D__UINT_LEAST32_TYPE__="unsigned int"
# zlib for ARM from library/zlib's pristine archive
ZLIB_TGZ ?= $(abspath $(HERE)/../../../../library/zlib/zlib-1.3.1.tar.gz)
ZLIB    := $(OUT)/zlib-1.3.1
CFLAGS  := $(CPU) $(OPT) $(DEFS) $(EXTRA) $(INT32) -ffunction-sections -fdata-sections -fno-short-enums -MMD -MP \
           -I$(HERE)/compat -include $(HERE)/compat/c1_compat.h -I$(HERE) $(INCFLAGS) -I$(ZLIB)
CXXFLAGS := $(CFLAGS) -fpermissive -fexceptions -fno-rtti -std=gnu++11 -w

ifeq ($(TARGET),host)
LDFLAGS := $(CPU) --specs=rdimon.specs -Wl,--gc-sections,--no-enum-size-warning,-Map=$(OUT)/uaehost.map
GLUE    := $(HERE)/c1uae.c $(HERE)/host_main.c
else
# the core 1 harness (bench.h, lscript.ld) and the firmware's BSP
Z3660_CORE1 ?= $(HOME)/Sources/usr-amix/amix-gate/usr/src/firmware/core1
Z3660   ?= $(HOME)/Sources/usr-amix/upstream/Z3660-beta21
HB      := $(abspath $(Z3660_CORE1))/cores/h264bench
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1
CFLAGS  += -I$(BSP)/include -I$(HB)
LDFLAGS := $(CPU) -T $(HB)/lscript.ld -specs=$(HB)/Xilinx.spec -L$(BSP)/lib \
           -Wl,--gc-sections,-build-id=none,--no-enum-size-warning,-Map=$(OUT)/uae.map,--defsym=_HEAP_SIZE=0x05000000
GLUE    := $(HERE)/c1uae.c $(HERE)/core1_main.c $(HERE)/c1_syscalls.c
endif

# upstream's list, without what has no meaning on core 1
DROP    := %/bsdsocket_host.o %/bsdsocket.o %/cda_play.o %/mp3decoder.o \
           %/sdltruetypefont.o %/thread.o %/sigsegv_handler.o \
           %/blkdev_cdimage.o %/fsusage.o %/cd32_fmv.o %/cd32_fmv_genlock.o
UOBJS   := $(patsubst src/%,$(SRC)/src/%,$(filter-out $(DROP),$(OBJS)) $(SOURCES_C:.c=.o))
OBJ_O   := $(patsubst $(SRC)/%,$(OUT)/%,$(UOBJS)) \
           $(patsubst $(HERE)/%.c,$(OUT)/glue/%.o,$(GLUE)) \
           $(OUT)/glue/c1_thread.o $(OUT)/glue/c1_stubs.o

all: $(OUT)/done

$(ZLIB)/zlib.h: $(ZLIB_TGZ)
	@mkdir -p $(OUT)
	tar xzf $< -C $(OUT)
	touch $@
ZSRC    := adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil \
           gzlib gzread gzwrite gzclose
$(OUT)/libz.a: $(ZLIB)/zlib.h
	cd $(ZLIB) && for f in $(ZSRC); do $(CC) $(CPU) -O2 -ffunction-sections -fdata-sections \
	    -DNO_GZCOMPRESS -DZ_HAVE_UNISTD_H -c $$f.c -o $$f.o || exit 1; done
	$(CROSS_COMPILE)ar rcs $@ $(patsubst %,$(ZLIB)/%.o,$(ZSRC))
$(OBJ_O): $(ZLIB)/zlib.h

$(OUT)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -w -c $< -o $@
$(OUT)/%.o: $(SRC)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -c $< -o $@
$(OUT)/glue/%.o: $(HERE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -c $< -o $@
$(OUT)/glue/%.o: $(HERE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifeq ($(TARGET),host)
$(OUT)/done: $(OUT)/uaehost.elf
	touch $@
$(OUT)/uaehost.elf: $(OBJ_O) $(OUT)/libz.a
	$(CXX) $(LDFLAGS) $^ $(OUT)/libz.a -lm -o $@
	$(SIZE) $@
else
$(OUT)/xil-crt0.o: $(BSP)/libsrc/standalone_v9_0/src/xil-crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -I$(BSP)/include -c $< -o $@
$(OUT)/done: $(OUT)/z3660ARM_Core_uae.bin
	touch $@
$(OUT)/uae.elf: $(OBJ_O) $(OUT)/xil-crt0.o $(OUT)/libz.a
	$(CXX) $(LDFLAGS) $^ -Wl,--start-group,-lxil,-lstdc++,-lgcc,-lm,-lc,--end-group -o $@
	$(SIZE) $@
$(OUT)/z3660ARM_Core_uae.bin: $(OUT)/uae.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j '.ARM.extab*' -j .ARM.exidx -j .init_array -j .fini_array $< $@
	ls -l $@
endif

-include $(OBJ_O:.o=.d)

clean:
	rm -rf build

.PHONY: all clean
