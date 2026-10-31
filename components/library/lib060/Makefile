# lib060 -- 68060 replacements for slow AMIX libc/libm routines.
#   libm060.a  sin cos tan fmod (+f): no FSIN/FCOS/FMOD, which the 060 traps on;
#              sqrt sqrtf with FSQRT; atanf through atan
#   libc060.a  memcpy memmove memset memcmp: longs instead of AMIX's byte loops
# Link both before -lm; compile callers with -fno-builtin-sin -fno-builtin-cos
# -fno-builtin-tan -fno-builtin-fmod (and the f variants) so gcc calls them.
CC = $(HOME)/opt/m68k-amix/bin/m68k-elf-gcc
AR = $(HOME)/opt/m68k-amix/bin/m68k-elf-ar
CFLAGS = -O2 -m68060 -fno-builtin -fno-tree-loop-distribute-patterns -Wall

all: libm060.a libc060.a

libm060.a: m060.o
	rm -f $@
	$(AR) rcs $@ m060.o

libc060.a: mem060.o
	rm -f $@
	$(AR) rcs $@ mem060.o

m060.o: m060.c
	$(CC) $(CFLAGS) -c m060.c

mem060.o: mem060.c
	$(CC) $(CFLAGS) -c mem060.c

test060: test060.c libm060.a libc060.a
	$(CC) -O2 -m68060 -fno-builtin -o $@ test060.c -L. -lm060 -lc060 -lm

clean:
	rm -f *.o *.a test060
