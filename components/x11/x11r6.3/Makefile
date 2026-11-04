# X11R6.3 for AMIX and the Xzz9000 server for ZZ9000 and Z3660 RTG.
COMPONENT_NAME    = x11r6.3
COMPONENT_VERSION = 6.3

include ../../../make-rules/shared.mk
include ../../../make-rules/shlib.mk

# the prepared tree, built on AMIX (see README.md)
prep:
	sh install.sh $(BUILD_DIR)/x11r6.3

# The client libraries as shared ones are files/build-shared.sh's, in
# SHARED_STAGE (SHARED-LIBRARIES.md); the package takes them, the headers and
# the .pc files from there.  libXtInherit.a is built on the Amiga by its own
# gcc (native.sh): gcc 14's is for GNU ld only.  libXp is x11/libxp.
X = $(SHARED_STAGE)/usr/x11r6
build:
	@test -f $(X)/lib/libX11.so.7 || { echo "no shared X in $(SHARED_STAGE): see SHARED-LIBRARIES.md"; exit 1; }

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/x11r6/lib/pkgconfig $(STUB_DIR)
	cp -P $(X)/lib/lib*.so.7 $(X)/lib/lib*.so $(STAGE_DIR)/usr/x11r6/lib/
	cp $(filter-out %/xp.pc,$(wildcard $(X)/lib/pkgconfig/*.pc)) $(STAGE_DIR)/usr/x11r6/lib/pkgconfig/
	cp -R $(X)/include $(STAGE_DIR)/usr/x11r6/
	$(call link_stubs,$(STAGE_DIR)/usr/x11r6/lib/lib*.so.7,/usr/x11r6/lib,$(STAGE_DIR)/usr/x11r6/lib/lib*.so.7)
	cp files/xtinherit.c $(STUB_DIR)/
	printf '%s\n' '# libXtInherit.a by the Amiga'"'"'s own gcc' \
		'gcc -O -I/usr/x11r6/include -c $$1/xtinherit.c && ar rcs /usr/x11r6/lib/libXtInherit.a xtinherit.o' \
		> $(STUB_DIR)/native.sh
	chmod -R u+w,go-w,go+rX $(STAGE_DIR)
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) \
		-cf build/$(COMPONENT_NAME)-amix.tar usr

.PHONY: prep build stage
