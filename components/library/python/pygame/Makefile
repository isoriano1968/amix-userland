# pygame-ce for the system Python 3.11 of AMIX (runtime/python3): the C
# modules cross-built with gcc 14 against the shared libSDL2, SDL2_image and
# freetype, pygame.font through freetype (ftfont), no mixer, no midi.
#
# AMIX's libm.a is static: what pygame takes from it is linked into each
# module, so lib060's libm060.a comes first (atan, pow, hypot ... without the
# FMOVECR the 060 traps on), and --exclude-libs keeps those copies local.
# numpy (library/python/numpy) is needed at run time only, by
# pygame.surfarray and pygame.sndarray; the build does not use it.
#
# DRAFT, 2026-11-07: every module compiles and links against the shared
# libSDL2-2.0.so.0 (library/sdl2) and libSDL2_image-2.0.so.0
# (library/sdl2-image); tools/amix-pyext-check finds every undefined symbol
# in libSDL2, libfreetype, libc or python3, and no 060-trapping instruction.
# Never run on AMIX yet.  The interpreter does load a C extension module there
# (runtime/python3 exports its C API with -Wl,-E; tested 2026-11-07).
#   - the sysroot needs runtime/python3's headers, _sysconfigdata and
#     python3.11 (for the check), and library/lib060's libm060.a
#   - HOST_PYTHON: CPython 3.11 for the build machine, CYTHON: Cython 3
#     (pygame._sdl2); see make-rules/python-ext.mk
#   - untested whether AMIX's ld.so.1 finds libSDL2's X and Pth libraries
#     through the library's runpath when python3 dlopens a module, or only
#     through the interpreter's (then python3 needs /usr/x11r6/lib and
#     /usr/local/gnu/lib in its runpath, or LD_LIBRARY_PATH)
COMPONENT_NAME    = pygame
COMPONENT_VERSION = 2.5.8
COMPONENT_ARCHIVE = pygame_ce-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://files.pythonhosted.org/packages/26/2d/0f942ec31d558a6a1f2fd0df9965ff0055f165ed5b8d36f6509b1f3768a2/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 3c8e69088ead310037972c391306ea58e74d7296b35d1890067749235fd554ba
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../../make-rules/shared.mk
include ../../../../make-rules/python-ext.mk

export PATH := $(AMIX_TOOLS):$(PATH)
TOOLS       = $(abspath ../../../../tools)
S           = build/pygame_ce-$(COMPONENT_VERSION)

# meson's cross file: AMIX's compiler, the sysroot's pkg-config (through
# build/pkg-config: no -lsocket, -lnsl), the Python
# headers and their AMIX compatibility headers, ours forced in
define pygame_cross
[binaries]
c = '$(AMIX_TOOLS)/m68k-elf-gcc'
ar = '$(AMIX_TOOLS)/m68k-elf-ar'
strip = '$(AMIX_TOOLS)/m68k-elf-strip'
pkg-config = '$(CURDIR)/build/pkg-config'
python = '$(CURDIR)/build/amixpy'

[built-in options]
c_args = ['-m68060', '-fPIC', '-fcommon', '-U__linux__', '-Ulinux', '-D__amix__', '-DSVR4', '-O2', '-fno-strict-aliasing', '-std=gnu11', '-I$(PYINC)/amixcompat', '-I$(AMIX_SYSROOT)/usr/local/gnu/include', '-include', '$(PYINC)/amix-python.h', '-I$(CURDIR)/files/amix', '-include', '$(CURDIR)/files/amix/amix-pygame.h']
c_link_args = ['-L$(AMIX_SYSROOT)$(PREFIX)/lib', '-lm060', '-Wl,--exclude-libs,ALL']

[properties]
sys_root = '$(AMIX_SYSROOT)'

[host_machine]
system = 'amix'
cpu_family = 'm68k'
cpu = '68060'
endian = 'big'
endef
export pygame_cross

build: $(COMPONENT_ARCHIVE)
	rm -rf build
	mkdir -p build
	tar -C build -xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -d $(S) -p1 < $$p || exit 1; done
	printf '%s\n' "$$pygame_cross" > build/amix-cross.ini
	$(pyext_glue)
	meson setup build/meson $(S) --cross-file=$(CURDIR)/build/amix-cross.ini \
		--native-file=$(CURDIR)/build/native.ini --prefix=$(PREFIX) --buildtype=plain -Dmixer=disabled -Dfont=disabled -Dmidi=disabled \
		-Dstripped=true > build/meson.log 2>&1 || { tail -20 build/meson.log; exit 1; }
	ninja -C build/meson > build/make.log 2>&1 || \
		{ grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }
	find build/meson -name '*.o' | xargs $(TOOLS)/amix-got-check
	$(TOOLS)/amix-pyext-check `find build/meson -name '*.cpython-311.so'`

stage:
	rm -rf $(STAGE_DIR)
	DESTDIR=$(STAGE_DIR) meson install -C build/meson --no-rebuild --tags=runtime,python-runtime,pg-tag \
		> build/install.log 2>&1 || { tail -20 build/install.log; exit 1; }
	for m in `find $(STAGE_DIR) -name '*.so'`; do m68k-elf-strip $$m && \
		$(TOOLS)/amix-fix-needed $$m > /dev/null || exit 1; done
	chmod -R go-w $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: build stage
