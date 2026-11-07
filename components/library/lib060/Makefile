# lib060 -- 68060 replacements for slow or trapping AMIX libc/libm routines.
#   libm060.a  sin cos tan fmod (+f): no FSIN/FCOS/FMOD, which the 060 traps on;
#              sqrt sqrtf with FDSQRT (m060.c, our own code);
#              atan atan2 atanf atan2f hypot pow erf erfc lgamma (gamma, signgam)
#              j0 j1 y0 y1 jn yn: musl 1.2.5's (MIT), for AMIX's libm members that
#              hold FMOVECR (atan.o atanf.o hypot.o pow.o erf.o gamma.o j0.o
#              j1.o; jn.o, which calls them), which the 040/060 traps on
#   libc060.a  memcpy memmove memset memcmp: longs instead of AMIX's byte loops
# Link both before -lm; compile callers with -fno-builtin-sin -fno-builtin-cos
# -fno-builtin-tan -fno-builtin-fmod (and the f variants) so gcc calls them.
#
# The musl part is built from the pristine release (the same archive and
# checksum as library/libm99) and patches/: only src/math and the headers it
# needs are unpacked into build/.  Each AMIX member is replaced by one object
# with the same global names (and AMIX's _j0, _lgamma ... aliases, which its
# jn.o calls), made by files/m060-group.sh: musl's helpers (exp, log, sin,
# __rem_pio2, the tables) are local copies inside it, so lib060 calls nothing
# of AMIX's libm and nothing of libm99, and exports no name libm99.so.1 does.
# The only outside call is sqrt (lib060's own, or the host's on the PC).
# Unlike AMIX's (SVID), they set no errno and call no matherr: C99 Annex F
# results only (pow(0,-1) = inf, y0(0) = -inf, lgamma(-1) = inf ...).
# hypotf erff erfcf lgammaf are not here: AMIX has none, libm99 exports them
# (trap-free), and lib060 adds no name libm99.so.1 exports.
# patches/: 0001 FLT_EVAL_METHOD 0 on the 040/060 (libm99's 0001), 0002
# atanf(+-inf) and atanf of |x| >= 2^26 rounded to pi/2 (musl gives 0.6 ulp
# below).
#
#   make [all]          libm060.a libc060.a, scanned for 060-trapping instructions
#   make test060 mbench.amix mbench060.amix   the box programs (not installed)
#   make native-test    the musl part for x86-64 against MPFR (correctly rounded)
#                       and glibc: ulp errors on the PC
#   make expected       test060-expected.h, the cases test060 checks on the box
#   make stage          build/stage and build/lib060-amix.tar
COMPONENT_NAME    = lib060
COMPONENT_VERSION = 1.1
# musl, for atan ... j1
COMPONENT_ARCHIVE = musl-1.2.5.tar.gz
COMPONENT_URL     = https://musl.libc.org/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
S                 = musl-1.2.5

UNPACK_MEMBERS = $(S)/COPYRIGHT $(S)/include $(S)/tools/mkalltypes.sed \
	$(S)/arch/m68k $(S)/arch/generic $(S)/arch/x86_64 \
	$(S)/src/math $(S)/src/include/features.h $(S)/src/internal/libm.h

CLEAN_FILES = $(S) $(COMPONENT_ARCHIVE) *.o *.a test060 *.amix

# the default, before shared.mk's targets
all:

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk

export PATH := $(AMIX_TOOLS):$(PATH)
TOOLS = $(abspath ../../../tools)

CC = m68k-elf-gcc
AR = m68k-elf-ar
# -fno-jump-tables: gcc puts switch tables in .text, which amix-fpu-scan would
# read as instructions
CFLAGS = -O2 -m68060 -fno-builtin -fno-tree-loop-distribute-patterns -fno-jump-tables -Wall

# musl's own flags, its headers only (as library/libm99)
MUSL_CFLAGS = -std=c99 -nostdinc -ffreestanding -fexcess-precision=standard -frounding-math \
	-fno-stack-protector -D_XOPEN_SOURCE=700 -w
musl_inc = -I$(S)/arch/$(1) -I$(S)/arch/generic -I$(2)/include -I$(S)/src/include \
	-I$(S)/src/internal -I$(S)/include
MCFLAGS = -O2 -m68060 -fno-jump-tables $(MUSL_CFLAGS) $(call musl_inc,m68k,build)

# one group per AMIX libm member: musl sources, exports, aliases (new=old)
GROUPS = atan atanf hypot pow erf lgamma bessel
EXP_SRCS = exp exp_data __math_oflow __math_uflow __math_xflow
LOG_SRCS = log log_data __math_divzero __math_invalid
G_atan    = atan atan2 fabs
X_atan    = atan atan2
G_atanf   = atanf atan2f fabsf
X_atanf   = atanf atan2f
G_hypot   = hypot
X_hypot   = hypot
G_pow     = pow fabs pow_data exp_data __math_oflow __math_uflow __math_xflow __math_invalid \
	__math_divzero
X_pow     = pow
G_erf     = erf fabs $(EXP_SRCS)
X_erf     = erf erfc
A_erf     = _erf=erf _erfc=erfc
G_lgamma  = lgamma_r __sin __cos floor $(LOG_SRCS) lgamma060
X_lgamma  = lgamma signgam
A_lgamma  = gamma=lgamma _lgamma=lgamma _gamma=lgamma
G_bessel  = j0 j1 jn fabs sin cos __sin __cos __rem_pio2 __rem_pio2_large scalbn floor $(LOG_SRCS)
X_bessel  = j0 y0 j1 y1 jn yn
A_bessel  = _j0=j0 _y0=y0 _j1=j1 _y1=y1 _jn=jn _yn=yn
EXTERNALS = sqrt
GROUP_SRCS = $(sort $(foreach g,$(GROUPS),$(G_$(g))))
MUSL_OBJS  = $(foreach g,$(GROUPS),build/m060-$(g).o)

all: libm060.a libc060.a
	$(TOOLS)/amix-fpu-scan libm060.a libc060.a

build: all
	$(MAKE) test060 mbench.amix mbench060.amix

# the musl tree, unpacked and patched; its bits/alltypes.h for m68k and x86-64
build/unpacked: $(COMPONENT_ARCHIVE) $(PATCHES)
	$(unpack)
	mkdir -p build/include/bits build/native/include/bits build/obj build/native/obj
	sed -f $(S)/tools/mkalltypes.sed $(S)/arch/m68k/bits/alltypes.h.in \
		$(S)/include/alltypes.h.in > build/include/bits/alltypes.h
	sed -f $(S)/tools/mkalltypes.sed $(S)/arch/x86_64/bits/alltypes.h.in \
		$(S)/include/alltypes.h.in > build/native/include/bits/alltypes.h
	touch $@

musl_src = $(if $(filter lgamma060,$(1)),lgamma060.c,$(S)/src/math/$(1).c)

build/obj/%.o: build/unpacked
	$(CC) $(MCFLAGS) -c $(call musl_src,$*) -o $@
build/obj/lgamma060.o: lgamma060.c

define group_rule
build/m060-$(1).o: $(foreach s,$(G_$(1)),build/obj/$(s).o) files/m060-group.sh
	LD=m68k-elf-ld OBJCOPY=m68k-elf-objcopy NM=m68k-elf-nm sh files/m060-group.sh \
		$$@ "$(X_$(1))" "$(A_$(1))" "$(EXTERNALS)" $(foreach s,$(G_$(1)),build/obj/$(s).o)
build/native/m060-$(1).o: $(foreach s,$(G_$(1)),build/native/obj/$(s).o) files/m060-group.sh
	sh files/m060-group.sh $$@ "$(X_$(1))" "$(A_$(1))" "$(EXTERNALS)" \
		$(foreach s,$(G_$(1)),build/native/obj/$(s).o)
endef
$(foreach g,$(GROUPS),$(eval $(call group_rule,$(g))))

libm060.a: m060.o $(MUSL_OBJS)
	rm -f $@
	$(AR) rcs $@ m060.o $(MUSL_OBJS)

libc060.a: mem060.o
	rm -f $@
	$(AR) rcs $@ mem060.o

m060.o: m060.c
	$(CC) $(CFLAGS) -c m060.c

mem060.o: mem060.c
	$(CC) $(CFLAGS) -c mem060.c

# the box programs: test060 (correctness, against test060-expected.h, and
# speed), mbench.amix (AMIX libm alone) and mbench060.amix (libm060 first)
test060: test060.c test060-expected.h libm060.a libc060.a
	$(CC) -O2 -m68060 -fno-builtin -fno-jump-tables -Wall -o $@ test060.c -L. -lm060 -lc060 -lm
	$(TOOLS)/amix-fpu-scan $@ > build/test060.scan || true
mbench.amix: mbench.c
	$(CC) -O2 -m68060 -fno-builtin -o $@ mbench.c -lm
mbench060.amix: mbench.c libm060.a
	$(CC) -O2 -m68060 -fno-builtin -o $@ mbench.c -L. -lm060 -lm

# ---- on the PC ---------------------------------------------------------------
# The same musl files and groups for x86-64 (generic C, no x86 assembler), the
# exports renamed m060_<name>, against MPFR (the PC's libmpfr.so.6, no headers
# needed) and glibc.  Both evaluate each double operation in double
# (FLT_EVAL_METHOD 0, no fused multiply-add), so the bits should be the
# 68060's, given the same sqrt.
HOSTCC        = cc
NATIVE_CFLAGS = -O2 $(MUSL_CFLAGS) $(call musl_inc,x86_64,build/native)
NATIVE_OBJS   = $(foreach g,$(GROUPS),build/native/m060-$(g).o)

build/native/obj/%.o: build/unpacked
	$(HOSTCC) $(NATIVE_CFLAGS) -c $(call musl_src,$*) -o $@
build/native/obj/lgamma060.o: lgamma060.c

build/native/native060: native060.c $(NATIVE_OBJS)
	ld -r -o build/native/m060.o $(NATIVE_OBJS)
	for g in $(foreach g,$(GROUPS),$(X_$(g)) $(foreach a,$(A_$(g)),$(firstword $(subst =, ,$(a))))); do \
		echo "$$g m060_$$g"; done > build/native/rename
	objcopy --redefine-syms=build/native/rename build/native/m060.o
	$(HOSTCC) -O1 -std=gnu11 -fno-builtin -Wall -o $@ native060.c build/native/m060.o \
		-l:libmpfr.so.6 -lm

NRAND = 100000
native-test: build/native/native060
	build/native/native060 -n $(NRAND) | tee build/native/result.txt

expected: build/native/native060
	build/native/native060 -n 2000 -g test060-expected.h > /dev/null

# build and stage as in the other components: the archives into /usr/local/lib
stage: all
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/local/lib $(STAGE_DIR)/usr/local/share/doc/lib060
	cp libm060.a libc060.a $(STAGE_DIR)/usr/local/lib/
	cp lib060.license $(STAGE_DIR)/usr/local/share/doc/lib060/
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/lib060-amix.tar usr
	$(manifest_files)

.PHONY: all build stage native-test expected
