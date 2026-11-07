# The start of every build: the archive unpacked fresh and patches/ applied,
# in order.  A recipe's build rule begins with $(unpack).
#
#   S        the unpacked tree, default <name>-<version>
#   PATCHES  default patches/*.patch, sorted; applied with patch -p1 in $(S)
#   UNPACK_MEMBERS  only these paths from the archive (default all of it)

S       ?= $(COMPONENT_NAME)-$(COMPONENT_VERSION)
PATCHES ?= $(sort $(wildcard patches/*.patch))
CLEAN_FILES ?= $(S) $(COMPONENT_ARCHIVE)

UNPACK_FLAGS = $(if $(filter %.tar.xz %.txz,$(COMPONENT_ARCHIVE)),xJf,$(if \
	$(filter %.tar.bz2 %.tbz2,$(COMPONENT_ARCHIVE)),xjf,xzf))

define unpack
	rm -rf $(S) build
	tar $(UNPACK_FLAGS) $(COMPONENT_ARCHIVE) $(UNPACK_MEMBERS)
	for p in $(PATCHES); do (cd $(S) && patch -p1 < $(CURDIR)/$$p) || exit 1; done
	mkdir -p build
endef
