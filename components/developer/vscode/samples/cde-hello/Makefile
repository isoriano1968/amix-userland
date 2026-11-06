# Makefile -- hello, a Motif/CDE "Hello, world" for AMIX, cross-built on Linux.
#
#   make                    gcc 14 (gcc14-cross-amix, m68k-elf-gcc, -m68060)
#   make TOOLCHAIN=gcc272   gcc 2.7.2.3 (m68k-cbm-sysv4-gcc, -m68020)
#   make clean
#
# The VS Code AMIX extension sets AMIX_TOOLCHAIN, AMIX_SYSROOT,
# AMIX_MOTIF_SYSROOT and the PATH for the toolchain chosen in "amix.toolchain".
# By hand, put the toolchain's bin directory on PATH.
#
# Motif and X11R6.3 come from the gcc 14 sysroot (shared libraries only) for
# both compilers: the gcc 2.7.2.3 sysroot has AMIX's own X11R5 but no Motif.
# On the A3000 they are in /usr/motif/lib and /usr/x11r6/lib; the runpath
# points there.

TOOLCHAIN     ?= $(or $(AMIX_TOOLCHAIN),gcc14)
MOTIF_SYSROOT ?= $(or $(AMIX_MOTIF_SYSROOT),$(HOME)/opt/m68k-amix-sysroot)

M = $(MOTIF_SYSROOT)/usr/motif
X = $(MOTIF_SYSROOT)/usr/x11r6

CPPFLAGS += -DFUNCPROTO=15 -I$(M)/include -I$(X)/include

# the libraries of ztopcde (amix-userland components/desktop/cde-apps);
# libamixc99.a gives libXm its snprintf
XLIBS = -lXm -lXmu -lXtInherit -lXt -lSM -lICE -lXext -lX11 \
	-lsocket -lnsl -L$(MOTIF_SYSROOT)/usr/local/lib -lamixc99 -lgen -lm

PYTHON ?= python3

ifeq ($(TOOLCHAIN),gcc14)
# ---- gcc 14: the driver links ----------------------------------------------
GCC = m68k-elf-gcc
# X11R6.3's headers predate C99: gcc 14 would warn about every implicit int
CFLAGS ?= -O2 -m68060 -fno-strict-aliasing -Wno-implicit-int \
	-Wno-implicit-function-declaration -Wno-incompatible-pointer-types \
	-Wno-int-conversion
LINK = $(CC) -o $@ hello.o -L$(M)/lib -L$(X)/lib \
	-Wl,-R,/usr/motif/lib -Wl,-R,/usr/x11r6/lib -Wl,-rpath-link,$(X)/lib \
	$(XLIBS)

else ifeq ($(TOOLCHAIN),gcc272)
# ---- gcc 2.7.2.3: ld links -------------------------------------------------
# The m68k-cbm-sysv4-gcc wrapper links only libc.so.1, but AMIX's -lc is the
# archive usr/ccs/lib/libc.so: libc.so.1 plus static members (select, syslog,
# gettimeofday, ...) that libsocket, libXt and libamixc99 need.  So the link
# is ld's, with crt1/crti/crtn, both halves of libc and libgcc.a, as AMIX's
# own cc does it.
GCC = m68k-cbm-sysv4-gcc
LD272   ?= m68k-cbm-sysv4-ld
SYSROOT ?= $(or $(AMIX_SYSROOT),$(HOME)/opt/amix-cross/m68k-cbm-sysv4/sysroot)
CRT_DIR ?= $(or $(AMIX_CRT_DIR),$(SYSROOT)/usr/ccs/lib)
CFLAGS  ?= -O -m68020
# libgcc.a beside the compiler (the wrapper has no -print-libgcc-file-name)
LIBGCC  ?= $(lastword $(sort $(wildcard $(dir $(shell command -v $(GCC)))../lib/gcc-lib/m68k-cbm-sysv4/*/libgcc.a)))
LINK = $(LD272) -dynamic-linker /usr/lib/libc.so.1 -o $@ \
	$(CRT_DIR)/crt1.o $(CRT_DIR)/crti.o hello.o \
	-rpath /usr/motif/lib:/usr/x11r6/lib -rpath-link $(X)/lib \
	-L$(M)/lib -L$(X)/lib -L$(SYSROOT)/usr/lib -L$(CRT_DIR) $(XLIBS) \
	$(SYSROOT)/usr/lib/libc.so.1 $(CRT_DIR)/libc.so $(LIBGCC) \
	$(CRT_DIR)/crtn.o

else
$(error TOOLCHAIN must be gcc14 or gcc272, not '$(TOOLCHAIN)')
endif

# make's built-in CC is "cc"; take the toolchain's unless CC was given
ifeq ($(origin CC),default)
CC = $(GCC)
endif

all: hello

hello.o: hello.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c hello.c -o $@

# AMIX's ld.so.1 reads the NEEDED names early in .dynstr, GNU ld puts them
# last: amix-fix-needed moves them to the front (from amix-userland/tools)
hello: hello.o
	$(LINK)
	$(PYTHON) amix-fix-needed $@

clean:
	rm -f hello hello.o

.PHONY: all clean
