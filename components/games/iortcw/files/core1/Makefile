# iortcw (Return to Castle Wolfenstein, single player) for the Z3660's ARM
# core 1: the whole engine, bare metal (newlib), Cortex-A9 + NEON, hard
# float.  One image, z3660ARM_Core_wolf.bin, run by c1q3 (games/ioquake3)
# with -core: the same harness, files, protocol, allocator and engine glue
# as Quake III's image (games/ioquake3/files/core1, built here with
# C1_WOLF), iortcw's engine, renderer and modules, the paks by request.
#
#   make -f files/core1/Makefile SRC=<patched iortcw SP tree> OUT=<dir> Z3660=... Z3660_CORE1=...
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

SRC     ?= $(error SRC= the patched iortcw SP tree)
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OUT     ?= $(CURDIR)/build/core1
C       := $(SRC)/code
# the harness and glue shared with Quake III's image
Q3C1    := $(abspath $(HERE)/../../../ioquake3/files/core1)

Z3660_CORE1 ?= $(HOME)/Sources/usr-amix/amix-gate/usr/src/firmware/core1
Z3660   ?= $(HOME)/Sources/usr-amix/upstream/Z3660-beta21
HB      := $(abspath $(Z3660_CORE1))/cores/h264bench
MESA    := $(abspath $(Z3660_CORE1))
OSMESA  := $(MESA)/build/arm/libOSMesa.a
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1

CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
OPT     ?= -O2
DEFS    := -D__z3660core__ -DC1_WOLF '-DC1_CORE_NAME="wolf"' -DBOTLIB -DUSE_INTERNAL_JPEG \
           -DUSE_INTERNAL_ZLIB -DUSE_INTERNAL_SDL_HEADERS -DUSE_FILE32API -DNO_VM_COMPILED -DNO_GZIP \
           -DNDEBUG '-DPRODUCT_VERSION="1.51-c1"' '-DC1FS_CACHE_MAX=(12u << 20)'
INC     := -I$(Q3C1)/compat -I$(Q3C1) -I$(C) -I$(MESA)/src/mesa-18.3.6/include -I$(BSP)/include -I$(HB) \
           -I$(C)/jpeg-8c -I$(C)/zlib-1.2.11
CFLAGS  := $(CPU) $(OPT) $(DEFS) -fno-strict-aliasing -ffunction-sections -fdata-sections \
           -fno-short-enums -MMD -MP -Wno-implicit-function-declaration -Wno-int-conversion \
           -Wno-incompatible-pointer-types $(INC)
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti -fpermissive -w
# above the image up to RELOAD_TRAMP (0x3FF00000), less the stack: see `make ... size`
HEAP_SIZE ?= 0x04700000
LDFLAGS := $(CPU) -T $(HB)/lscript.ld -specs=$(HB)/Xilinx.spec -L$(BSP)/lib \
           -Wl,--gc-sections,-build-id=none,--no-enum-size-warning,-Map=$(OUT)/wolf.map \
           -Wl,--defsym=_HEAP_SIZE=$(HEAP_SIZE),--defsym=_STACK_SIZE=0x00400000

ENGINE := \
  $(addprefix client/,cl_cgame.c cl_cin.c cl_console.c cl_input.c cl_keys.c cl_main.c cl_net_chan.c \
    cl_parse.c cl_scrn.c cl_ui.c cl_avi.c snd_adpcm.c snd_dma.c snd_mem.c snd_mix.c snd_wavelet.c \
    snd_main.c snd_codec.c snd_codec_wav.c snd_openal.c) \
  $(addprefix qcommon/,cm_load.c cm_patch.c cm_polylib.c cm_test.c cm_trace.c cmd.c common.c cvar.c \
    files.c md4.c md5.c msg.c net_chan.c huffman.c q_math.c q_shared.c puff.c vm.c vm_interpreted.c) \
  $(addprefix server/,sv_bot.c sv_ccmds.c sv_client.c sv_game.c sv_init.c sv_main.c sv_net_chan.c \
    sv_snapshot.c sv_world.c) \
  $(addprefix botlib/,be_aas_bspq3.c be_aas_cluster.c be_aas_debug.c be_aas_entity.c be_aas_file.c \
    be_aas_main.c be_aas_move.c be_aas_optimize.c be_aas_reach.c be_aas_route.c be_aas_routealt.c \
    be_aas_routetable.c be_aas_sample.c be_ai_char.c be_ai_chat.c be_ai_gen.c be_ai_goal.c be_ai_move.c \
    be_ai_weap.c be_ai_weight.c be_ea.c be_interface.c l_crc.c l_libvar.c l_log.c l_memory.c \
    l_precomp.c l_script.c l_struct.c) \
  $(addprefix renderer/,tr_animation.c tr_backend.c tr_bsp.c tr_cmds.c tr_cmesh.c tr_curve.c tr_flares.c \
    tr_font.c tr_image.c tr_image_bmp.c tr_image_jpg.c tr_image_pcx.c tr_image_png.c tr_image_tga.c \
    tr_init.c tr_light.c tr_main.c tr_marks.c tr_mesh.c tr_model.c tr_model_iqm.c tr_noise.c tr_scene.c \
    tr_shade.c tr_shade_calc.c tr_shader.c tr_shadows.c tr_sky.c tr_surface.c tr_world.c)
SPLINES := $(addprefix splines/,math_angles.cpp math_matrix.cpp math_quaternion.cpp math_vector.cpp \
    q_parse.cpp splines.cpp util_str.cpp)
JPEG   := $(notdir $(filter-out %/jmemansi.c %/jmemdos.c %/jmemmac.c %/jmemname.c %/jpegtran.c, \
            $(wildcard $(C)/jpeg-8c/j*.c)))
ZLIB   := adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c unzip.c ioapi.c
GLUE   := core1_main.c c1fs.c sys_c1.c net_c1.c in_c1.c snd_c1.c glimp_c1.c c1_malloc.c

.DEFAULT_GOAL := all

# the modules' sources, from upstream's Makefile (lists.py)
$(OUT)/lists.mk: $(HERE)/lists.py $(SRC)/Makefile
	@mkdir -p $(OUT)
	python3 -I $(HERE)/lists.py $(SRC) > $@
include $(OUT)/lists.mk

DLMALLOC ?= $(abspath $(HERE)/../../../../library/dlmalloc/malloc-2.8.6.c)
DLFLAGS := -DHAVE_MMAP=0 -DHAVE_MREMAP=0 -DHAVE_MORECORE=1 -DMORECORE=c1p_morecore \
           -DMORECORE_CONTIGUOUS=0 -DMORECORE_CANNOT_TRIM=1 -DUSE_LOCKS=0 -DNO_MALLOC_STATS=1 \
           -DDEFAULT_GRANULARITY=0x100000 -DLACKS_SYS_MMAN_H=1 -Dmalloc_getpagesize=4096 \
           -fno-builtin -include $(Q3C1)/c1_morecore.h

E_OBJ  := $(patsubst %.c,$(OUT)/engine/%.o,$(ENGINE)) $(patsubst %.cpp,$(OUT)/engine/%.o,$(SPLINES)) \
          $(patsubst %.c,$(OUT)/jpeg/%.o,$(JPEG)) $(patsubst %.c,$(OUT)/zlib/%.o,$(ZLIB)) \
          $(patsubst %.c,$(OUT)/glue/%.o,$(GLUE)) $(OUT)/glue/armgl_compat.o $(OUT)/xil-crt0.o \
          $(OUT)/glue/dlmalloc.o
MODS   := $(OUT)/mod/cgame.o $(OUT)/mod/qagame.o $(OUT)/mod/ui.o

all: $(OUT)/z3660ARM_Core_wolf.bin

$(OUT)/engine/%.o: $(C)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -w -c $< -o $@
$(OUT)/engine/%.o: $(C)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/jpeg/%.o: $(C)/jpeg-8c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/zlib/%.o: $(C)/zlib-1.2.11/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/glue/%.o: $(Q3C1)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -Wno-unused-function -c $< -o $@
$(OUT)/glue/armgl_compat.o: $(MESA)/compat/arm/armgl_compat.c
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -O2 -fno-short-enums -ffunction-sections -fdata-sections -I$(MESA)/compat/arm \
	    -include $(MESA)/compat/arm/armgl_compat.h -c $< -o $@
$(OUT)/glue/dlmalloc.o: $(DLMALLOC)
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -O2 -ffunction-sections -fdata-sections -fno-short-enums $(DLFLAGS) -c $< -o $@
$(OUT)/xil-crt0.o: $(BSP)/libsrc/standalone_v9_0/src/xil-crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -I$(BSP)/include -c $< -o $@

# the modules, as Quake III's (module.ld): one object each, only dllEntry
# and vmMain left, as <mod>_dllEntry / <mod>_vmMain
define module
$(OUT)/mod-$(1)/%.o: $(C)/%.c
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) -std=gnu99 -w $(2) -c $$< -o $$@
$(OUT)/mod/$(1).o: $(patsubst %.c,$(OUT)/mod-$(1)/%.o,$(3)) $(Q3C1)/module.ld
	@mkdir -p $(OUT)/mod
	sed 's/MOD/$(1)/g' $(Q3C1)/module.ld > $(OUT)/mod/$(1).ld
	$(LD) -r -T $(OUT)/mod/$(1).ld -o $(OUT)/mod/$(1).r.o $(patsubst %.c,$(OUT)/mod-$(1)/%.o,$(3))
	$(OBJCOPY) -G dllEntry -G vmMain -G $(1)_data_start -G $(1)_data_end -G $(1)_bss_start \
	    -G $(1)_bss_end $(OUT)/mod/$(1).r.o $(OUT)/mod/$(1).g.o
	$(OBJCOPY) --redefine-sym dllEntry=$(1)_dllEntry --redefine-sym vmMain=$(1)_vmMain \
	    $(OUT)/mod/$(1).g.o $$@
endef
$(eval $(call module,cgame,-DCGAMEDLL -DCGAME,$(CGAME)))
$(eval $(call module,qagame,-DGAMEDLL -DQAGAME,$(QAGAME)))
$(eval $(call module,ui,-DUI,$(UI)))

$(OUT)/wolf.elf: $(E_OBJ) $(MODS) $(OSMESA)
	$(CXX) $(LDFLAGS) $(E_OBJ) $(MODS) $(OSMESA) -Wl,--start-group,-lxil,-lstdc++,-lgcc,-lm,-lc,--end-group -o $@
	$(SIZE) $@
$(OUT)/z3660ARM_Core_wolf.bin: $(OUT)/wolf.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j '.ARM.extab*' -j .ARM.exidx -j .init_array -j .fini_array $< $@
	@ls -l $@
	@test $$(stat -c %s $@) -le 12582912 || { echo "image over HB_IMAGE_MAX (12 MB)"; rm -f $@; exit 1; }

size: $(OUT)/wolf.elf
	@$(CROSS_COMPILE)nm $< | awk '$$3 == "_heap_start" || $$3 == "_heap_end" || $$3 == "_stack" || $$3 == "__fiq_stack" { print }'

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)

.PHONY: all size
