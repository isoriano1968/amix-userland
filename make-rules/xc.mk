# xc.mk -- R6.3 headers out of X11R6.3's own xc-1.tar.gz (x.org, pinned by
# x11/x11r6.3's SOURCES.sha256, the download cache of x11/x11r6.3) for the
# X libraries that need what the x11r6.3 package does not install
# (extutil.h, Xlibint.h, region.h), as x11/libxp does.  A recipe sets
# XC_MEMBERS, makes build depend on $(XC_TGZ) and runs $(xc_extract) after
# $(unpack); the headers are then under $(X11_SRC).
XC_ARCHIVE = xc-1.tar.gz
XC_DIR    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../components/x11/x11r6.3)
XC_SHA256  = $(shell awk '$$2 == "$(XC_ARCHIVE)" { print $$1 }' $(XC_DIR)/SOURCES.sha256)
XC_URL     = https://www.x.org/archive/X11R6.3/tars/$(XC_ARCHIVE)
DISTFILES ?= $(XC_DIR)/distfiles
XC_TGZ     = $(DISTFILES)/$(XC_ARCHIVE)
X11_SRC    = $(CURDIR)/build/xc

$(XC_TGZ):
	mkdir -p $(DISTFILES)
	curl -fsL -o $@.tmp $(XC_URL)
	echo "$(XC_SHA256)  $@.tmp" | sha256sum -c --quiet -
	mv $@.tmp $@

define xc_extract
	test -n "$(XC_SHA256)" && echo "$(XC_SHA256)  $(XC_TGZ)" | sha256sum -c --quiet -
	gzip -dc $(XC_TGZ) | tar -C build -xf - $(XC_MEMBERS)
endef
