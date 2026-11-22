# Mesa 18.3 for the Z3660's core 1 and AMIX: softpipe with NEON fast paths.
COMPONENT_NAME    = mesa
COMPONENT_VERSION = 18.3.6
COMPONENT_ARCHIVE = mesa-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://archive.mesa3d.org/older-versions/18.x/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = aaf17638dcf5a90b93b6389e152fdc9ef147768b09598f24d2c5cf482fcfc705
# amix-gate's core1 at a pinned commit: Mesa's patch (zz3660, the NEON paths),
# build/xbuild.py, libgl-amix.sh and zzgl; patches/ makes their compilers and
# the X headers settable (AMIX_TOOLS, ARM, X11_INCLUDE)
GATE_COMMIT       = ecb6fcf36ba7017bf695c0244d979ab26aa5b2f9
GATE_ARCHIVE      = amix-gate-$(GATE_COMMIT).tar.gz
GATE_URL          = https://github.com/isoriano1968/amix-gate/archive/$(GATE_COMMIT).tar.gz
GATE_SHA256       = 31f472a71136a8c9e39a5d91795b077c9036ebabde485a0e73b75f450e475906
CLEAN_FILES       = $(COMPONENT_ARCHIVE) $(GATE_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk
include ../../../make-rules/z3660.mk

# build/core1: the gate's core1 with Mesa unpacked and patched in src/, as
# xbuild.py wants it; games/ioquake3 and games/iortcw build against it too
# (make arm: build/core1/build/arm/libOSMesa.a)
C1   = $(BUILD_DIR)/core1
MESA = $(C1)/src/mesa-$(COMPONENT_VERSION)
C1ENV = AMIX_TOOLS=$(AMIX_TOOLS) ARM=$(ARM) X11_INCLUDE=$(AMIX_SYSROOT)/usr/x11r6/include \
	AMIX_USERLAND_TOOLS=$(TOOLS)
JOBS ?= 4

$(GATE_ARCHIVE):
	curl -fL --retry 3 -o $@.part $(GATE_URL)
	echo "$(GATE_SHA256)  $@.part" | sha256sum -c -
	mv $@.part $@

$(C1)/.unpacked: $(COMPONENT_ARCHIVE) $(GATE_ARCHIVE)
	rm -rf $(C1)
	mkdir -p $(C1)/src
	tar xzf $(GATE_ARCHIVE) -C $(C1) --strip-components=5 \
		amix-gate-$(GATE_COMMIT)/usr/src/firmware/core1
	for p in patches/*.patch; do patch -s -p1 -d $(C1) < $$p || exit 1; done
	tar xJf $(COMPONENT_ARCHIVE) -C $(C1)/src
	patch -s -p1 -d $(MESA) < $(C1)/patches/mesa-$(COMPONENT_VERSION)-amix-z3660.patch
	mkdir -p $(C1)/zzgl/gen
	python3 -I $(C1)/zzgl/gen.py $(C1)/zzgl/gen
	touch $@

# Mesa's own meson builds for the PC (host meson, ninja and python3-mako), of
# which xbuild.py takes the sources, flags and generated files: only the
# generated sources are built here, not the PC libraries
MESON_COMMON = -Ddri-drivers= -Dgallium-drivers=swrast -Dllvm=false -Degl=false -Dgbm=false \
	-Dgles1=false -Dgles2=false -Dvulkan-drivers= -Dshared-glapi=false
gen_sources = ninja -C $(1) `ninja -C $(1) -t targets all | awk -F: '/CUSTOM_COMMAND/ && $$1 ~ /\.(c|cpp|h)$$/ { print $$1 }'`
$(C1)/.hosts: $(C1)/.unpacked
	rm -rf $(C1)/src/host-osmesa $(C1)/src/host-xlib
	cd $(MESA) && meson setup ../host-osmesa $(MESON_COMMON) -Dplatforms= -Dosmesa=gallium \
		-Dglx=disabled -Dbuildtype=debugoptimized -Db_ndebug=true > /dev/null
	cd $(MESA) && meson setup ../host-xlib $(MESON_COMMON) -Dplatforms=x11 -Dosmesa=none \
		-Dglx=gallium-xlib -Dbuildtype=release -Dglvnd=false -Ddri3=false -Dvalgrind=false \
		-Dlibunwind=false -Dlmsensors=false -Dgallium-xvmc=false -Dgallium-vdpau=false \
		-Dgallium-omx=disabled -Dgallium-va=false -Dgallium-nine=false -Dgallium-xa=false > /dev/null
	$(call gen_sources,$(C1)/src/host-osmesa) > /dev/null
	$(call gen_sources,$(C1)/src/host-xlib) > /dev/null
	touch $@

# core 1's Mesa (the ARM), for the programs that run there
arm: $(C1)/.hosts
	$(C1ENV) python3 -I $(C1)/build/xbuild.py arm -j $(JOBS)

# AMIX's libGL.so.18 (GLX on X, zz3660 to core 1) and libzzgl.so.1
build: $(C1)/.hosts
	$(C1ENV) python3 -I $(C1)/build/xbuild.py amix-glx -j $(JOBS)
	$(C1ENV) sh $(C1)/build/libgl-amix.sh $(AMIX_SYSROOT)
	rm -rf $(BUILD_DIR)/zzgl
	$(C1ENV) sh $(C1)/zzgl/build-shared.sh $(BUILD_DIR)/zzgl $(TOOLS)

# The package: libGL.so.18 with its headers and gl.pc (build-package.sh), and
# libzzgl.so.1 with its headers and zzgl.pc
Z = $(BUILD_DIR)/zzgl/usr/local
stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/local/lib/pkgconfig $(STAGE_DIR)/usr/local/include
	sh build-package.sh $(STAGE_DIR) $(C1) > /dev/null
	cp -P $(Z)/lib/libzzgl.so.1 $(Z)/lib/libzzgl.so $(STAGE_DIR)/usr/local/lib/
	cp $(Z)/lib/pkgconfig/zzgl.pc $(STAGE_DIR)/usr/local/lib/pkgconfig/
	cp -R $(Z)/include/zzgl $(STAGE_DIR)/usr/local/include/
	$(TOOLS)/amix-fix-needed $(STAGE_DIR)/usr/local/lib/libGL.so.18 $(STAGE_DIR)/usr/local/lib/libzzgl.so.1 > /dev/null
	export STUB_ONLY='^(gl[A-Z]|glX|GL|OSMesa|zzgl_)' && $(call link_stubs,$(STAGE_DIR)/usr/local/lib/libGL.so.18 $(STAGE_DIR)/usr/local/lib/libzzgl.so.1,/usr/local/lib,$(STAGE_DIR)/usr/local/lib/lib*.so.1* $(AMIX_SYSROOT)/usr/x11r6/lib/lib*.so.7)
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: arm build stage
