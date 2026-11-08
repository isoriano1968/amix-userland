# ioquake3 for the Z3660's ARM core 1: the whole engine, bare metal
# (newlib), Cortex-A9 + NEON, hard float.  One image, z3660ARM_Core_q3.bin:
#
#   engine      client, server, botlib, collision, sound mixing (code/)
#   modules     cgame, qagame, ui linked in (no dlopen), each one relocatable
#               object with its own globals (module.ld, sys_c1.c)
#   renderer    renderergl1 on Mesa 18.3's OSMesa/softpipe (amix-gate
#               firmware/core1 build/arm/libOSMesa.a), called directly
#   platform    core1_main.c (the board), c1fs.c, sys_c1.c, net_c1.c,
#               in_c1.c, snd_c1.c, glimp_c1.c
#
#   make -f files/core1/Makefile SRC=<patched ioq3 tree> OUT=<dir> Z3660=... Z3660_CORE1=...
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
CXX     := $(CROSS_COMPILE)g++
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

SRC     ?= $(error SRC= the patched ioq3 tree)
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OUT     ?= $(CURDIR)/build/core1
C       := $(SRC)/code

Z3660_CORE1 ?= $(HOME)/Sources/usr-amix/amix-gate/usr/src/firmware/core1
Z3660   ?= $(HOME)/Sources/usr-amix/upstream/Z3660-beta21
HB      := $(abspath $(Z3660_CORE1))/cores/h264bench
MESA    := $(abspath $(Z3660_CORE1))
OSMESA  := $(MESA)/build/arm/libOSMesa.a
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1

# Cortex-A9: ARMv7-A with NEON, no integer divide (never ARM_HAS_DIV);
# full-size enums as Mesa's ARM build (-fno-short-enums)
CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
OPT     ?= -O2
DEFS    := -D__z3660core__ -DBOTLIB -DUSE_INTERNAL_JPEG -DUSE_INTERNAL_ZLIB -DUSE_INTERNAL_SDL_HEADERS \
           -DNO_GZIP -DNDEBUG '-DPRODUCT_VERSION="1.36-c1"'
CFLAGS  := $(CPU) $(OPT) $(DEFS) -std=gnu99 -fno-strict-aliasing -ffunction-sections -fdata-sections \
           -fno-short-enums -MMD -MP -Wno-implicit-function-declaration -Wno-int-conversion \
           -Wno-incompatible-pointer-types \
           -I$(HERE)/compat -I$(HERE) -I$(C) -I$(MESA)/src/mesa-18.3.6/include -I$(BSP)/include -I$(HB) \
           -I$(C)/thirdparty/jpeg-9f -I$(C)/thirdparty/zlib-1.3.1
# above the image up to RELOAD_TRAMP (0x3FF00000), less the stack: see `make ... size`
HEAP_SIZE ?= 0x06000000
LDFLAGS := $(CPU) -T $(HB)/lscript.ld -specs=$(HB)/Xilinx.spec -L$(BSP)/lib \
           -Wl,--gc-sections,-build-id=none,--no-enum-size-warning,-Map=$(OUT)/q3.map \
           -Wl,--defsym=_HEAP_SIZE=$(HEAP_SIZE),--defsym=_STACK_SIZE=0x00400000

ENGINE := \
  qcommon/cm_load.c qcommon/cm_patch.c qcommon/cm_polylib.c qcommon/cm_test.c qcommon/cm_trace.c \
  qcommon/cmd.c qcommon/common.c qcommon/cvar.c qcommon/files.c qcommon/md4.c qcommon/md5.c \
  qcommon/msg.c qcommon/net_chan.c qcommon/huffman.c qcommon/q_math.c qcommon/q_shared.c \
  qcommon/unzip.c qcommon/ioapi.c qcommon/vm.c qcommon/vm_interpreted.c \
  server/sv_bot.c server/sv_client.c server/sv_ccmds.c server/sv_game.c server/sv_init.c \
  server/sv_main.c server/sv_net_chan.c server/sv_snapshot.c server/sv_world.c \
  $(addprefix botlib/,be_aas_bspq3.c be_aas_cluster.c be_aas_debug.c be_aas_entity.c \
    be_aas_file.c be_aas_main.c be_aas_move.c be_aas_optimize.c be_aas_reach.c be_aas_route.c \
    be_aas_routealt.c be_aas_sample.c be_ai_char.c be_ai_chat.c be_ai_gen.c be_ai_goal.c \
    be_ai_move.c be_ai_weap.c be_ai_weight.c be_ea.c be_interface.c l_crc.c l_libvar.c l_log.c \
    l_memory.c l_precomp.c l_script.c l_struct.c) \
  $(addprefix client/,cl_cgame.c cl_cin.c cl_console.c cl_input.c cl_keys.c cl_main.c \
    cl_net_chan.c cl_parse.c cl_scrn.c cl_ui.c cl_avi.c snd_adpcm.c snd_dma.c snd_mem.c \
    snd_mix.c snd_wavelet.c snd_main.c snd_codec.c snd_codec_wav.c snd_openal.c) \
  $(addprefix renderercommon/,tr_font.c tr_image_bmp.c tr_image_jpg.c tr_image_pcx.c \
    tr_image_png.c tr_image_pvr.c tr_image_tga.c tr_noise.c puff.c) \
  $(addprefix renderergl1/,tr_animation.c tr_backend.c tr_bsp.c tr_cmds.c tr_curve.c \
    tr_flares.c tr_image.c tr_init.c tr_light.c tr_main.c tr_marks.c tr_mesh.c tr_model.c \
    tr_model_iqm.c tr_scene.c tr_shade.c tr_shade_calc.c tr_shader.c tr_shadows.c tr_sky.c \
    tr_surface.c tr_world.c)
JPEG   := $(notdir $(filter-out %/jmemansi.c %/jmemdos.c %/jmemmac.c %/jmemname.c %/jpegtran.c, \
            $(wildcard $(C)/thirdparty/jpeg-9f/j*.c)))
ZLIB   := $(notdir $(filter-out $(C)/thirdparty/zlib-1.3.1/gz%, $(wildcard $(C)/thirdparty/zlib-1.3.1/*.c)))
GLUE   := core1_main.c c1fs.c sys_c1.c net_c1.c in_c1.c snd_c1.c glimp_c1.c c1_malloc.c
# dlmalloc 2.8.6 from library/dlmalloc: the heap's two regions as segments
DLMALLOC ?= $(abspath $(HERE)/../../../../library/dlmalloc/malloc-2.8.6.c)
DLFLAGS := -DHAVE_MMAP=0 -DHAVE_MREMAP=0 -DHAVE_MORECORE=1 -DMORECORE=c1p_morecore \
           -DMORECORE_CONTIGUOUS=0 -DMORECORE_CANNOT_TRIM=1 -DUSE_LOCKS=0 -DNO_MALLOC_STATS=1 \
           -DDEFAULT_GRANULARITY=0x100000 -DLACKS_SYS_MMAN_H=1 -Dmalloc_getpagesize=4096 \
           -fno-builtin -include $(HERE)/c1_morecore.h
# -fno-builtin: GCC otherwise turns calloc's malloc + memset into a call to calloc

CGAME  := cgame/cg_main.c game/bg_misc.c game/bg_pmove.c game/bg_slidemove.c game/bg_lib.c \
  $(addprefix cgame/,cg_consolecmds.c cg_draw.c cg_drawtools.c cg_effects.c cg_ents.c cg_event.c \
    cg_info.c cg_localents.c cg_marks.c cg_particles.c cg_players.c cg_playerstate.c cg_predict.c \
    cg_scoreboard.c cg_servercmds.c cg_snapshot.c cg_view.c cg_weapons.c cg_syscalls.c) \
  qcommon/q_math.c qcommon/q_shared.c
QAGAME := game/g_main.c $(addprefix game/,ai_chat.c ai_cmd.c ai_dmnet.c ai_dmq3.c ai_main.c ai_team.c \
    ai_vcmd.c bg_misc.c bg_pmove.c bg_slidemove.c bg_lib.c g_active.c g_arenas.c g_bot.c g_client.c \
    g_cmds.c g_combat.c g_items.c g_mem.c g_misc.c g_missile.c g_mover.c g_session.c g_spawn.c \
    g_svcmds.c g_target.c g_team.c g_trigger.c g_utils.c g_weapon.c g_syscalls.c) \
  qcommon/q_math.c qcommon/q_shared.c
UI     := q3_ui/ui_main.c game/bg_misc.c game/bg_lib.c $(addprefix q3_ui/,ui_addbots.c ui_atoms.c \
    ui_cdkey.c ui_cinematics.c ui_confirm.c ui_connect.c ui_controls2.c ui_credits.c ui_demo2.c \
    ui_display.c ui_gameinfo.c ui_ingame.c ui_loadconfig.c ui_menu.c ui_mfield.c ui_mods.c \
    ui_network.c ui_options.c ui_playermodel.c ui_players.c ui_playersettings.c ui_preferences.c \
    ui_qmenu.c ui_removebots.c ui_saveconfig.c ui_serverinfo.c ui_servers2.c ui_setup.c ui_sound.c \
    ui_sparena.c ui_specifyserver.c ui_splevel.c ui_sppostgame.c ui_spskill.c ui_startserver.c \
    ui_team.c ui_teamorders.c ui_video.c) ui/ui_syscalls.c qcommon/q_math.c qcommon/q_shared.c

E_OBJ  := $(patsubst %.c,$(OUT)/engine/%.o,$(ENGINE)) \
          $(patsubst %.c,$(OUT)/jpeg/%.o,$(JPEG)) $(patsubst %.c,$(OUT)/zlib/%.o,$(ZLIB)) \
          $(patsubst %.c,$(OUT)/glue/%.o,$(GLUE)) $(OUT)/glue/armgl_compat.o $(OUT)/xil-crt0.o \
          $(OUT)/glue/dlmalloc.o
MODS   := $(OUT)/mod/cgame.o $(OUT)/mod/qagame.o $(OUT)/mod/ui.o

all: $(OUT)/z3660ARM_Core_q3.bin

$(OUT)/engine/%.o: $(C)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/jpeg/%.o: $(C)/thirdparty/jpeg-9f/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/zlib/%.o: $(C)/thirdparty/zlib-1.3.1/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@
$(OUT)/glue/%.o: $(HERE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wno-unused-function -c $< -o $@
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

# the modules: compiled with their own define, linked into one object
# (module.ld), only dllEntry and vmMain left global, as <mod>_dllEntry /
# <mod>_vmMain (plus the bounds of its globals)
define module
$(OUT)/mod-$(1)/%.o: $(C)/%.c
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) -w -D$(2) -c $$< -o $$@
$(OUT)/mod/$(1).o: $(patsubst %.c,$(OUT)/mod-$(1)/%.o,$(3)) $(HERE)/module.ld
	@mkdir -p $(OUT)/mod
	sed 's/MOD/$(1)/g' $(HERE)/module.ld > $(OUT)/mod/$(1).ld
	$(LD) -r -T $(OUT)/mod/$(1).ld -o $(OUT)/mod/$(1).r.o $(patsubst %.c,$(OUT)/mod-$(1)/%.o,$(3))
	$(OBJCOPY) -G dllEntry -G vmMain -G $(1)_data_start -G $(1)_data_end -G $(1)_bss_start \
	    -G $(1)_bss_end $(OUT)/mod/$(1).r.o $(OUT)/mod/$(1).g.o
	$(OBJCOPY) --redefine-sym dllEntry=$(1)_dllEntry --redefine-sym vmMain=$(1)_vmMain \
	    $(OUT)/mod/$(1).g.o $$@
endef
$(eval $(call module,cgame,CGAME,$(CGAME)))
$(eval $(call module,qagame,QAGAME,$(QAGAME)))
$(eval $(call module,ui,UI,$(UI)))

$(OUT)/q3.elf: $(E_OBJ) $(MODS) $(OSMESA)
	$(CXX) $(LDFLAGS) $(E_OBJ) $(MODS) $(OSMESA) -Wl,--start-group,-lxil,-lstdc++,-lgcc,-lm,-lc,--end-group -o $@
	$(SIZE) $@
$(OUT)/z3660ARM_Core_q3.bin: $(OUT)/q3.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j '.ARM.extab*' -j .ARM.exidx -j .init_array -j .fini_array $< $@
	@ls -l $@
	@test $$(stat -c %s $@) -le 12582912 || { echo "image over HB_IMAGE_MAX (12 MB)"; rm -f $@; exit 1; }

# where the heap ends: _heap_end + stack must stay below RELOAD_TRAMP
size: $(OUT)/q3.elf
	@$(CROSS_COMPILE)nm $< | awk '$$3 == "_heap_start" || $$3 == "_heap_end" || $$3 == "_stack" || $$3 == "__fiq_stack" { print }'

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)

.PHONY: all size
