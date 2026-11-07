# libm99: the C99 maths AMIX lacks, from musl 1.2.5's libm (MIT).
#
# AMIX's libm.a and libc (SVR4.0, 1991) have the C89 double functions, 21
# float ones and a few extras (cbrt, rint, hypot, erf, lgamma ...).  This
# gives the rest of C99 <math.h> (float and long double variants, expm1,
# log2, round, lrint, fma, tgamma, nextafter ...), all of <complex.h> and
# <fenv.h>, as libm99.so.1 in /usr/local/lib, with headers in
# /usr/local/include/amix-c99 that add to AMIX's <math.h> (#include_next)
# and libm99.pc that puts them first.  gcc 14 only for the library;
# files/include/math.h says what works with gcc 2.7.
#
# Exports: only files/exports; nothing AMIX's libm or libc already has, and
# not lib060's sin/cos/tan/fmod.  Everything else in the library, musl's
# own exp, log, pow, sin, cos, atan2, sqrt ... that the exported functions
# call, is made local (objcopy), so libm99 never calls AMIX's libm (static
# only, and its atan, hypot, lgamma, erfc and pow's _exp__E hold FMOVECR,
# which the 060 traps on) and never takes over a program's sin or exp.
#
# Only src/math, src/complex, src/fenv and the headers they need are taken
# from the archive; SRCS_* is the closure of files/exports (an archive of all
# of src/math + src/complex linked with -u for each export).
#
# Patches: 0001 FLT_EVAL_METHOD 0 on the 040/060 (musl assumes the 68881's
# 2, which breaks round/rint with gcc -m68060), 0002 sqrt and sqrtf in
# hardware (fdsqrt/fssqrt) instead of musl's integer code, 0003 two musl bugs
# the native test found: tgammal's sign for x < -13 with a fraction above
# one half, and catanl returning its imaginary part through a float.
#
# Not for 68020/030 machines: -m68060 code uses fsadd/fdmul ..., which the
# 68881/68882 lack.  libgcc's non-PIC pieces (__muldi3, __muldc3 ...) leave
# text relocations, as in the other libraries; ld.so.1 takes them.
#
#   make build stage      the library, the headers, libm99.pc; the AMIX test
#                         program build/libm99-test (not installed)
#   make native-test      the same sources for x86-64 against glibc
#   make expected         tests/libm99-expected.h from native-test
#   make scan             060-trapping instructions in the objects (part of build)
COMPONENT_NAME    = libm99
COMPONENT_VERSION = 1.2.5
COMPONENT_ARCHIVE = musl-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://musl.libc.org/releases/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
S                 = musl-$(COMPONENT_VERSION)

UNPACK_MEMBERS = $(S)/COPYRIGHT $(S)/include $(S)/tools/mkalltypes.sed \
	$(S)/arch/m68k $(S)/arch/generic $(S)/arch/x86_64 \
	$(S)/src/math $(S)/src/complex $(S)/src/fenv $(S)/src/include/features.h \
	$(S)/src/internal/libm.h $(S)/src/internal/complex_impl.h $(S)/src/internal/atomic.h

include ../../../make-rules/shared.mk
include ../../../make-rules/unpack.mk
include ../../../make-rules/shlib.mk

export PATH := $(AMIX_TOOLS):$(PATH)

SRCS_MATH = acoshf acoshl acosl asinhf asinhl asinl atan2f atan2l atan2 atanf atanhf atanhl \
	atanl atan cbrtf cbrtl ceill copysignf copysignl copysign __cosdf cosf coshf coshl cosh \
	__cosl cosl __cos cos erff erfl exp2f_data exp2f exp2l exp2 exp_data expf expl expm1f \
	expm1l expm1 exp __expo2f __expo2 fabsf fabsl fabs fdimf fdiml fdim finitef finite floorf \
	floorl floor fmaf fmal fma fmaxf fmaxl fmax fminf fminl fmin fmodl __fpclassifyf \
	__fpclassifyl __fpclassify frexpf frexpl hypotf hypotl hypot ilogbf ilogbl ilogb \
	__invtrigl ldexpf ldexpl lgammaf lgammaf_r lgammal lgamma_r llrintf llrintl llrint \
	llroundf llroundl llround log10l log1pf log1pl log1p log2_data log2f_data log2f log2l \
	log2 logbf logbl logb log_data logf_data logf logl log lrintf lrintl lrint lroundf \
	lroundl lround __math_divzerof __math_divzero __math_invalidf __math_invalid \
	__math_oflowf __math_oflow __math_uflowf __math_uflow __math_xflowf __math_xflow modfl \
	nanf nanl nan nearbyintf nearbyintl nearbyint nextafterf nextafterl nextafter \
	nexttowardf nexttowardl nexttoward __polevll pow_data powl pow remainderf remainderl \
	__rem_pio2f __rem_pio2_large __rem_pio2l __rem_pio2 remquof remquol remquo rintf rintl \
	rint roundf roundl round scalbf scalblnf scalblnl scalbln scalbnf scalbnl scalbn scalb \
	__signbitf __signbitl __signbit signgam __sindf sinf sinhf sinhl sinh __sinl sinl __sin \
	sin sqrtf sqrtl sqrt __tandf tanf tanhl __tanl tanl __tan tan tgammaf tgammal tgamma \
	truncf truncl trunc
SRCS_COMPLEX = cabsf cabsl cabs cacosf cacoshf cacoshl cacosh cacosl cacos cargf cargl carg \
	casinf casinhf casinhl casinh casinl casin catanf catanhf catanhl catanh catanl catan \
	ccosf ccoshf ccoshl ccosh ccosl ccos __cexpf cexpf cexpl __cexp cexp cimagf cimagl cimag \
	clogf clogl clog conjf conjl conj cpowf cpowl cpow cprojf cprojl cproj crealf creall \
	creal csinf csinhf csinhl csinh csinl csin csqrtf csqrtl csqrt ctanf ctanhf ctanhl ctanh \
	ctanl ctan
SRCS_FENV = fegetexceptflag feholdexcept fesetexceptflag fesetround feupdateenv

# musl's own flags (its Makefile and configure), for an arch of ours: its
# headers only, no libc of the host's or AMIX's
MUSL_CFLAGS = -std=c99 -nostdinc -ffreestanding -fexcess-precision=standard -frounding-math \
	-fno-stack-protector -D_XOPEN_SOURCE=700 -w
musl_inc = -I$(S)/arch/$(1) -I$(S)/arch/generic -I$(2)/include -I$(S)/src/include \
	-I$(S)/src/internal -I$(S)/include
CC68   = m68k-elf-gcc
CFLAGS = -O2 -m68060 -fPIC $(MUSL_CFLAGS) $(call musl_inc,m68k,build)


SHLIBS                    = libm99.so.1
SHLIB_ARCHIVE_libm99.so.1 = build/libm99_pic.a
SHLIB_ARGS_libm99.so.1    =

TOOLS = $(abspath ../../../tools)
TEST_CFLAGS = -O2 -m68060 -Wall -Ifiles/include

build: $(COMPONENT_ARCHIVE)
	$(unpack)
	mkdir -p build/include/bits build/obj
	sed -f $(S)/tools/mkalltypes.sed $(S)/arch/m68k/bits/alltypes.h.in \
		$(S)/include/alltypes.h.in > build/include/bits/alltypes.h
	# src/math/m68k/<name>.c in place of src/math/<name>.c, as musl's Makefile does
	for f in $(SRCS_MATH); do s=$(S)/src/math/m68k/$$f.c; [ -f $$s ] || s=$(S)/src/math/$$f.c; \
		$(CC68) $(CFLAGS) -c $$s -o build/obj/$$f.o || exit 1; done
	for f in $(SRCS_COMPLEX); do $(CC68) $(CFLAGS) -c $(S)/src/complex/$$f.c -o build/obj/$$f.o || exit 1; done
	for f in $(SRCS_FENV); do $(CC68) $(CFLAGS) -c $(S)/src/fenv/$$f.c -o build/obj/$$f.o || exit 1; done
	$(CC68) $(CFLAGS) -c $(S)/src/fenv/m68k/fenv.c -o build/obj/fenv.o
	$(TOOLS)/amix-got-check build/obj/*.o
	$(TOOLS)/amix-fpu-scan build/obj/*.o
	# one object, only files/exports global (weak aliases made strong)
	m68k-elf-ld -r -o build/m99.o build/obj/*.o
	files/strong-exports.sh build/m99.o files/exports
	m68k-elf-ar rcs build/libm99_pic.a build/m99.o
	$(mkshlibs)
	$(MAKE) check-exports test-amix

# the dynamic symbols are files/exports exactly, none of them AMIX's
check-exports:
	m68k-elf-nm -D --defined-only build/libm99.so.1 | awk '$$2 ~ /[TDBRW]/ && $$3 !~ /^(_etext|_edata|_end|__bss_start|_init|_fini)$$/ {print $$3}' | sort > build/exports.got
	grep -v '^#' files/exports | grep . | sort > build/exports.want
	diff build/exports.want build/exports.got
	m68k-elf-nm -g --defined-only $(AMIX_SYSROOT)/usr/ccs/lib/libm.a $(AMIX_SYSROOT)/usr/ccs/lib/libc.a \
		2>/dev/null | awk 'NF == 3 {print $$3}' | sort -u | comm -12 - build/exports.got > build/exports.clash
	@test ! -s build/exports.clash || { echo "exported by AMIX too:"; cat build/exports.clash; exit 1; }
	m68k-elf-readelf -d build/libm99.so.1 | grep -E 'NEEDED|SONAME|TEXTREL' || true

# the test program for the Amiga, linked with the library in build/ and
# AMIX's libm; run it there as LD_LIBRARY_PATH=. ./libm99-test
# (-fno-jump-tables: gcc puts switch tables in .text, which the scan would
# read as instructions)
test-amix:
	$(CC68) $(TEST_CFLAGS) -fno-builtin -fno-jump-tables -c tests/libm99-test.c -Itests -o build/libm99-test.o
	$(TOOLS)/amix-fpu-scan build/libm99-test.o
	$(CC68) -m68060 -o build/libm99-test build/libm99-test.o -Lbuild -lm99 -lm -Wl,-R,$(PREFIX)/lib
	$(TOOLS)/amix-fix-needed build/libm99-test > /dev/null

scan:
	$(TOOLS)/amix-fpu-scan build/obj/*.o build/libm99.so.1

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/lib/pkgconfig $(STAGE_DIR)$(PREFIX)/include/amix-c99 \
		$(STAGE_DIR)$(PREFIX)/share/doc/libm99
	cp files/include/math.h files/include/complex.h files/include/fenv.h \
		$(STAGE_DIR)$(PREFIX)/include/amix-c99/
	cp $(S)/COPYRIGHT $(STAGE_DIR)$(PREFIX)/share/doc/libm99/COPYRIGHT.musl
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@VERSION@|$(COMPONENT_VERSION)|' files/libm99.pc.in \
		> $(STAGE_DIR)$(PREFIX)/lib/pkgconfig/libm99.pc
	$(stage_finish)
	$(manifest_files)

# ---- on the PC ---------------------------------------------------------------
# The same files for x86-64 (generic C; fenv from musl's x86_64 fenv.s), the
# exports renamed m99_<name>, against glibc.  Needs the unpacked tree (build).
HOSTCC     = cc
NATIVE     = build/native
NATIVE_CFLAGS = -O2 $(MUSL_CFLAGS) $(call musl_inc,x86_64,$(NATIVE))
# what the generic sqrt, sqrtf and sqrtl need, which the m68k ones do not
SRCS_NATIVE = sqrt_data __math_invalidl

native-test:
	@test -d $(S) || { echo "make build first"; exit 1; }
	rm -rf $(NATIVE) && mkdir -p $(NATIVE)/include/bits $(NATIVE)/obj
	sed -f $(S)/tools/mkalltypes.sed $(S)/arch/x86_64/bits/alltypes.h.in \
		$(S)/include/alltypes.h.in > $(NATIVE)/include/bits/alltypes.h
	for f in $(SRCS_MATH) $(SRCS_NATIVE); do $(HOSTCC) $(NATIVE_CFLAGS) -c $(S)/src/math/$$f.c -o $(NATIVE)/obj/$$f.o || exit 1; done
	for f in $(SRCS_COMPLEX); do $(HOSTCC) $(NATIVE_CFLAGS) -c $(S)/src/complex/$$f.c -o $(NATIVE)/obj/$$f.o || exit 1; done
	for f in $(SRCS_FENV); do $(HOSTCC) $(NATIVE_CFLAGS) -c $(S)/src/fenv/$$f.c -o $(NATIVE)/obj/$$f.o || exit 1; done
	$(HOSTCC) -c $(S)/src/fenv/x86_64/fenv.s -o $(NATIVE)/obj/fenv.o
	ld -r -o $(NATIVE)/m99.o $(NATIVE)/obj/*.o
	grep -v '^#' files/exports | grep . | awk '{print $$1, "m99_" $$1}' > $(NATIVE)/rename
	objcopy --keep-global-symbols=files/exports $(NATIVE)/m99.o
	objcopy --redefine-syms=$(NATIVE)/rename $(NATIVE)/m99.o
	$(HOSTCC) -O1 -std=c11 -fno-builtin -Itests -o $(NATIVE)/native-test tests/native-test.c \
		$(NATIVE)/m99.o -lm
	$(NATIVE)/native-test -n 400 | tee $(NATIVE)/result.txt | grep -v ' ok$$'

expected:
	@test -x $(NATIVE)/native-test || $(MAKE) native-test
	$(NATIVE)/native-test -n 400 -k 6 -g tests/libm99-expected.h > /dev/null || true

.PHONY: build stage check-exports test-amix scan native-test expected
