# mesa-demos for AMIX, on the Mesa 18 package: /opt/mesa-demos and the mesa-demo
# launcher.  SETS says which of its programs.  Needs x11/mesa-183,
# library/zlib and library/libpng in the sysroot.
COMPONENT_NAME    = mesa-demos
COMPONENT_VERSION = 60ad664
COMPONENT_ARCHIVE = mesa-demos-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://github.com/mirror/mesa-demos/archive/60ad66441a710e9da12ad1bba444ff4a5b564828.tar.gz
COMPONENT_SHA256  = 30dfa19abceb309356034325f70cf716bb8d695b6ff2d06ffbb0910667cb16ca
CLEAN_FILES       = mesa-demos-60ad66441a710e9da12ad1bba444ff4a5b564828 $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

SRC  = mesa-demos-60ad66441a710e9da12ad1bba444ff4a5b564828
SETS ?= glsl demos trivial tests redbook samples xdemos
OPT  = $(STAGE_DIR)/opt/mesa-demos

build: $(COMPONENT_ARCHIVE)
	rm -rf $(SRC) build
	tar xzf $(COMPONENT_ARCHIVE)
	sh build.sh $(CURDIR)/$(SRC) $(AMIX_SYSROOT) $(BUILD_DIR) $(SETS)

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(OPT)/bin $(OPT)/data $(OPT)/lib $(STAGE_DIR)/usr/local/bin
	for s in $(SETS); do \
		for f in build/$$s/*; do case $$f in *.err) ;; *) cp $$f $(OPT)/bin/;; esac; done; \
	done
	cp build/lib/libmesademo.so.1 $(OPT)/lib/
	cp $(SRC)/src/data/* $(SRC)/src/glsl/*.vert $(SRC)/src/glsl/*.frag $(SRC)/src/glsl/*.glsl \
		$(SRC)/src/glsl/*.shtest $(OPT)/data/
	rm -f $(OPT)/data/meson.build
	cp mesa-demo $(OPT)/
	ln -sf /opt/mesa-demos/mesa-demo $(STAGE_DIR)/usr/local/bin/mesa-demo

.PHONY: build stage
