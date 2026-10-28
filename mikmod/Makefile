# mikmod player for AMIX (Commodore Amiga UNIX, m68k SVR4)
#
# Same story as usr/local/src/libmikmod/Makefile: upstream's autoconf/
# automake build system has no knowledge of this target and there's no
# autotools available to regenerate it, so this is a plain hand-written
# SVR4 make Makefile instead, like ../libmikmod/Makefile.
#
# Requires libmikmod.a already built in ../libmikmod, and ncurses
# (curses.h and libncurses.a where gcc finds them).
#
# AMIX's SVR4 libc is missing a few functions this player assumes exist
# (fnmatch, getopt_long, usleep, strcasecmp); src/mfnmatch.c,
# src/getopt_long.c and src/musleep.c are upstream's own portable
# fallbacks for the first three (see config.h), and
# src/amix_strcasecmp.c is a small one written for this port to cover the
# fourth, since libmikmod's own strcasecmp fallback isn't exported under
# that name.

CC	= gcc
CFLAGS	= -O -m68020 -DHAVE_CONFIG_H -Dunix -I. -Isrc -I../libmikmod/include
LDFLAGS	= -L../libmikmod -lmikmod -lncurses -lm

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

OBJECTS = src/display.o \
	  src/marchive.o \
	  src/mikmod.o \
	  src/mlist.o \
	  src/mconfig.o \
	  src/mwindow.o \
	  src/mmenu.o \
	  src/mwidget.o \
	  src/mdialog.o \
	  src/mconfedit.o \
	  src/mutilities.o \
	  src/mplayer.o \
	  src/mlistedit.o \
	  src/rcfile.o \
	  src/mfnmatch.o \
	  src/getopt_long.o \
	  src/musleep.o \
	  src/amix_strcasecmp.o

mikmod: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

install: mikmod
	cpset mikmod $(ROOT)/usr/local/bin 755 bin bin
	cpset mikmodrc $(ROOT)/usr/local/share/mikmod 644 bin bin

clean:
	-rm -f $(OBJECTS) mikmod

clobber: clean
