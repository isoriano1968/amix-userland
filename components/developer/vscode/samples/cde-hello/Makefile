# Makefile -- hello, a Motif/CDE "Hello, world" for AMIX, cross-built on Linux.
#
#   make                    gcc 14 (gcc14-cross-amix, m68k-elf-gcc, -m68060)
#   make TOOLCHAIN=gcc272   gcc 2.7.2.3 (m68k-cbm-sysv4-gcc, -m68020)
#   make clean
#
# The VS Code AMIX extension sets AMIX_TOOLCHAIN, AMIX_SYSROOT,
# AMIX_MOTIF_SYSROOT and the PATH for the toolchain chosen in "amix.toolchain".
# By hand, put the toolchain's bin directory on PATH (/opt/m68k-amix/bin or
# /opt/amix-cross/bin from the OpenIndiana packages, else under ~/opt).
#
# Motif and X11R6.3 come from the gcc 14 sysroot (shared libraries only) for
# both compilers: the gcc 2.7.2.3 sysroot has AMIX's own X11R5 but no Motif.
# On the A3000 they are in /usr/motif/lib and /usr/x11r6/lib; the runpath
# points there.

TOOLCHAIN     ?= $(or $(AMIX_TOOLCHAIN),gcc14)

# the OpenIndiana package developer/amix/sysroot (/opt/m68k-amix-sysroot, with
# its marker) when installed, else the hand-built ~/opt layout
PKG_SYSROOT   := $(patsubst %/etc/amix-sysroot.release,%,$(wildcard /opt/m68k-amix-sysroot/etc/amix-sysroot.release))
MOTIF_SYSROOT ?= $(or $(AMIX_MOTIF_SYSROOT),$(PKG_SYSROOT),$(HOME)/opt/m68k-amix-sysroot)

M = $(MOTIF_SYSROOT)/usr/motif
X = $(MOTIF_SYSROOT)/usr/x11r6

CPPFLAGS += -DFUNCPROTO=15 -I$(M)/include -I$(X)/include

# the libraries of ztopcde (amix-userland components/desktop/cde-apps);
# libamixc99 (shared, /usr/lib/libamixc99.so.1) gives libXm its snprintf;
# gcc 2.7.2.3's own sysroot has none, so -L names the library sysroot's usr/lib
XLIBS = -lXm -lXmu -lXt -lSM -lICE -lXext -lX11 \
	-lsocket -lnsl -L$(MOTIF_SYSROOT)/usr/lib -lamixc99 -lgen -lm

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
# ---- gcc 2.7.2.3: the wrapper links ----------------------------------------
# m68k-cbm-sysv4-gcc links crt1/crti/crtn, libc.so.1 plus AMIX's libc archive
# usr/ccs/lib/libc.so (select, syslog, gettimeofday, ...) and libgcc.a itself,
# and passes -Wl, options to ld (gcc-cross-amix 83765c5 or later).
# ld 2.8.1 takes -R DIR only for a directory that exists on the build host,
# so the runpath is given with -rpath.
GCC = m68k-cbm-sysv4-gcc
CFLAGS ?= -O -m68020
LINK = $(CC) -o $@ hello.o -L$(M)/lib -L$(X)/lib \
	-Wl,-rpath,/usr/motif/lib:/usr/x11r6/lib -Wl,-rpath-link,$(X)/lib \
	$(XLIBS)

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
