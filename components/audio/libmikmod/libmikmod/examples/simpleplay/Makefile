# AMIX build of the splay.c example -- links against the static
# libmikmod.a built by usr/local/src/libmikmod/Makefile. Replaces
# upstream's Makefile, which used $(shell ...) (a GNU make extension SVR4
# make doesn't have) and a libmikmod-config script we don't build (see
# usr/local/src/libmikmod/README.md for why the autotools build is
# bypassed on this target).

CC	= gcc
CFLAGS	= -O -m68020 -Dunix -I../../include
LDFLAGS	= -L../.. -lmikmod

splay: splay.o
	$(CC) splay.o $(LDFLAGS) -o splay

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f splay.o splay
