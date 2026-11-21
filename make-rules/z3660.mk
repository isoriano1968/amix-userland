# z3660.mk -- what a core 1 program builds against besides amix-gate: the
# Z3660 firmware (shanshe/Z3660, v1.03 beta 21) for its Xilinx standalone BSP
# (libxil, headers, linker script), a pinned GitHub archive unpacked once
# under the tree's build/; and the ARM compiler: ARM, else AMIX_ARM, else
# ~/opt/arm-none-eabi/bin, else the one in PATH (Debian's gcc-arm-none-eabi
# is the same Arm GNU Toolchain 14.2.Rel1).  A recipe makes its build depend
# on $(Z3660_TREE) and passes Z3660=$(Z3660) on.
Z3660_COMMIT = 50e98e01dbd32db36c1d9fc947e5c2c0623aa334
Z3660_SHA256 = ec652e29c521d8fd13785d5818e873d59e1209ca3fea7d901c32a2abc9ed08c1
Z3660_URL    = https://github.com/shanshe/Z3660/archive/$(Z3660_COMMIT).tar.gz
Z3660_TOP   := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../build/z3660)
Z3660_TGZ    = $(Z3660_TOP)/Z3660-$(Z3660_COMMIT).tar.gz
Z3660       ?= $(Z3660_TOP)/Z3660-$(Z3660_COMMIT)
Z3660_TREE   = $(Z3660)/.unpacked
ARM         ?= $(or $(AMIX_ARM),$(wildcard $(HOME)/opt/arm-none-eabi/bin),$(patsubst %/,%,$(dir $(shell command -v arm-none-eabi-gcc))))

$(Z3660_TOP)/Z3660-$(Z3660_COMMIT)/.unpacked:
	mkdir -p $(Z3660_TOP)
	test -f $(Z3660_TGZ) || { curl -fsL -o $(Z3660_TGZ).tmp $(Z3660_URL) && mv $(Z3660_TGZ).tmp $(Z3660_TGZ); }
	echo "$(Z3660_SHA256)  $(Z3660_TGZ)" | sha256sum -c --quiet -
	rm -rf $(Z3660_TOP)/Z3660-$(Z3660_COMMIT) $(Z3660_TOP)/unpack.tmp
	mkdir $(Z3660_TOP)/unpack.tmp
	tar xzf $(Z3660_TGZ) -C $(Z3660_TOP)/unpack.tmp
	mv $(Z3660_TOP)/unpack.tmp/Z3660-$(Z3660_COMMIT) $(Z3660_TOP)/Z3660-$(Z3660_COMMIT)
	rmdir $(Z3660_TOP)/unpack.tmp
	touch $@
