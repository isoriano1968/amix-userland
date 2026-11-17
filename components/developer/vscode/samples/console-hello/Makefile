# Makefile -- hello, a curses console program for AMIX, cross-built on Linux.
#
#   make                    gcc 14 (gcc14-cross-amix, m68k-elf-gcc, -m68060)
#   make TOOLCHAIN=gcc272   gcc 2.7.2.3 (m68k-cbm-sysv4-gcc, -m68020)
#   make clean
#
# The VS Code AMIX extension sets AMIX_TOOLCHAIN, AMIX_MOTIF_SYSROOT (the
# library sysroot) and the PATH for the toolchain chosen in "amix.toolchain".
# By hand, put the toolchain's bin directory on PATH (/opt/m68k-amix/bin or
# /opt/amix-cross/bin from the OpenIndiana packages, else under ~/opt).
#
# curses is ncurses 6.6, amix-userland library/ncurses, shared in
# /usr/local/lib on AMIX (with its terminfo in /usr/share/terminfo).  It is
# taken from the gcc 14 sysroot for both compilers, with the flags pkg-config
# reads from its ncurses.pc there; that requires amixc99.pc, the shared
# /usr/lib/libamixc99.so.1 with -lgen -lm.  The sysroots' usr/include/curses.h
# is an older ncurses 5.4 header without a library, so usr/local/include,
# from ncurses.pc, comes first.

TOOLCHAIN ?= $(or $(AMIX_TOOLCHAIN),gcc14)

# the OpenIndiana package developer/amix/sysroot (/opt/m68k-amix-sysroot, with
# its marker) when installed, else the hand-built ~/opt layout
PKG_SYSROOT := $(patsubst %/etc/amix-sysroot.release,%,$(wildcard /opt/m68k-amix-sysroot/etc/amix-sysroot.release))
LIB_SYSROOT ?= $(or $(AMIX_MOTIF_SYSROOT),$(PKG_SYSROOT),$(HOME)/opt/m68k-amix-sysroot)

# pkg-config reads only the sysroot's .pc files (never the build host's) and
# prefixes their paths with the sysroot.  -L<sysroot>/usr/lib is kept
# (ALLOW_SYSTEM_LIBS): gcc 2.7.2.3's own sysroot has no libamixc99.
PKG_CONFIG ?= pkg-config
PC = PKG_CONFIG_PATH= \
	PKG_CONFIG_LIBDIR=$(LIB_SYSROOT)/usr/local/lib/pkgconfig:$(LIB_SYSROOT)/usr/lib/pkgconfig \
	PKG_CONFIG_SYSROOT_DIR=$(LIB_SYSROOT) PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 $(PKG_CONFIG)

NCURSES_CFLAGS := $(shell $(PC) --cflags ncurses)
NCURSES_LIBS   := $(shell $(PC) --libs ncurses)
ifeq ($(NCURSES_LIBS),)
$(error pkg-config finds no ncurses.pc in $(LIB_SYSROOT): install library/ncurses into the sysroot)
endif

CPPFLAGS += $(NCURSES_CFLAGS)
# libncurses.so.6 needs libamixc99.so.1: ld finds it through -rpath-link
LIBS      = $(NCURSES_LIBS) -Wl,-rpath-link,$(LIB_SYSROOT)/usr/lib

PYTHON ?= python3

ifeq ($(TOOLCHAIN),gcc14)
GCC    = m68k-elf-gcc
CFLAGS ?= -O2 -m68060 -fno-strict-aliasing
RPATH  = -Wl,-R,/usr/local/lib
else ifeq ($(TOOLCHAIN),gcc272)
# the wrapper links crt*, both halves of AMIX's libc and libgcc.a, and passes
# -Wl, to ld; ld 2.8.1 takes -R only for a directory on the build host
GCC    = m68k-cbm-sysv4-gcc
CFLAGS ?= -O -m68020
RPATH  = -Wl,-rpath,/usr/local/lib
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

# gcc 14's ld warns "/usr/lib/libc.so.1, needed by .../libncurses.so, not
# found": AMIX libraries name libc by that absolute path, which ld looks up on
# the build host.  Harmless: the program gets its own libc.so.1 NEEDED.
#
# AMIX's ld.so.1 reads the NEEDED names early in .dynstr, GNU ld puts them
# last: amix-fix-needed moves them to the front (from amix-userland/tools)
hello: hello.o
	$(CC) -o $@ hello.o $(RPATH) $(LIBS)
	$(PYTHON) amix-fix-needed $@

clean:
	rm -f hello hello.o

.PHONY: all clean
