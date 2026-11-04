# Mesa 18.3 for the Z3660's core 1 and AMIX: softpipe with NEON fast paths.
COMPONENT_NAME    = mesa
COMPONENT_VERSION = 18.3.6
COMPONENT_ARCHIVE = mesa-$(COMPONENT_VERSION).tar.xz
COMPONENT_URL     = https://archive.mesa3d.org/older-versions/18.x/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = aaf17638dcf5a90b93b6389e152fdc9ef147768b09598f24d2c5cf482fcfc705
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

# the patched source; core1-z3660-amix's build/xbuild.py compiles it
prep: $(COMPONENT_ARCHIVE)
	mkdir -p build
	tar xJf $(COMPONENT_ARCHIVE) -C build
	patch -s -p1 -d build/mesa-$(COMPONENT_VERSION) < patches/0001-amix-z3660.patch

# The package: libGL.so.18 from amix-gate (core1/build/libgl-amix.sh), with its
# headers and gl.pc (build-package.sh), and libzzgl.so.1 with its headers and
# zzgl.pc, which amix-gate's core1/zzgl/build-shared.sh put in SHARED_STAGE.
Z = $(SHARED_STAGE)/usr/local
stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/local/lib/pkgconfig $(STAGE_DIR)/usr/local/include
	sh build-package.sh $(STAGE_DIR) > /dev/null
	cp -P $(Z)/lib/libzzgl.so.1 $(Z)/lib/libzzgl.so $(STAGE_DIR)/usr/local/lib/
	cp $(Z)/lib/pkgconfig/zzgl.pc $(STAGE_DIR)/usr/local/lib/pkgconfig/
	cp -R $(Z)/include/zzgl $(STAGE_DIR)/usr/local/include/
	$(TOOLS)/amix-fix-needed $(STAGE_DIR)/usr/local/lib/libGL.so.18 $(STAGE_DIR)/usr/local/lib/libzzgl.so.1 > /dev/null
	export STUB_ONLY='^(gl[A-Z]|glX|GL|OSMesa|zzgl_)' && $(call link_stubs,$(STAGE_DIR)/usr/local/lib/libGL.so.18 $(STAGE_DIR)/usr/local/lib/libzzgl.so.1,/usr/local/lib,$(STAGE_DIR)/usr/local/lib/lib*.so.1* $(SHARED_STAGE)/usr/x11r6/lib/lib*.so.7)
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: prep stage
