# libmikmod for AMIX (Commodore Amiga UNIX, m68k SVR4)
#
# Upstream's autoconf/automake/libtool build system does not run on this
# target -- see config.h for why. This is a plain SVR4 make Makefile that
# compiles a static libmikmod.a from just the sources this port needs, in
# the style of AMIX's own source Makefiles.
#
# Only the drivers needed on AMIX are compiled in: drv_nos (the fallback
# that is always there), drv_raw/drv_wav/drv_stdout/drv_pipe (file and pipe
# writers, useful for testing without sound), drv_amix (/dev/audio) and
# drv_paula (/dev/paula).  The other drivers/drv_*.c files are for other
# systems and are not compiled.

CC	= gcc
CFLAGS	= -O -m68020 -DHAVE_CONFIG_H -Dunix -I. -Iinclude

# Without an explicit -o, cc writes foo.o for dir/foo.c into the current
# directory instead of dir/foo.o, so the default suffix rule doesn't work
# once sources are split across subdirectories -- spell it out instead.
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

DRIVERS = drivers/drv_nos.o \
	  drivers/drv_raw.o \
	  drivers/drv_wav.o \
	  drivers/drv_stdout.o \
	  drivers/drv_pipe.o \
	  drivers/drv_amix.o \
	  drivers/drv_paula.o

LOADERS = loaders/load_669.o \
	  loaders/load_amf.o \
	  loaders/load_dsm.o \
	  loaders/load_far.o \
	  loaders/load_gdm.o \
	  loaders/load_imf.o \
	  loaders/load_it.o \
	  loaders/load_m15.o \
	  loaders/load_med.o \
	  loaders/load_mod.o \
	  loaders/load_mtm.o \
	  loaders/load_okt.o \
	  loaders/load_s3m.o \
	  loaders/load_stm.o \
	  loaders/load_stx.o \
	  loaders/load_ult.o \
	  loaders/load_uni.o \
	  loaders/load_xm.o

MMIO	= mmio/mmalloc.o mmio/mmerror.o mmio/mmio.o

PLAYER	= playercode/mdreg.o \
	  playercode/mdriver.o \
	  playercode/mdulaw.o \
	  playercode/mloader.o \
	  playercode/mlreg.o \
	  playercode/mlutil.o \
	  playercode/mplayer.o \
	  playercode/munitrk.o \
	  playercode/mwav.o \
	  playercode/npertab.o \
	  playercode/sloader.o \
	  playercode/virtch.o \
	  playercode/virtch2.o \
	  playercode/virtch_common.o

POSIX	= posix/strcasecmp.o

OBJECTS = $(DRIVERS) $(LOADERS) $(MMIO) $(PLAYER) $(POSIX)

libmikmod.a: $(OBJECTS)
	ar r $@ $(OBJECTS)

install: libmikmod.a
	cpset libmikmod.a $(ROOT)/usr/local/lib 644 bin bin
	cpset include/mikmod.h $(ROOT)/usr/local/include 644 bin bin
	cpset include/mikmod_ctype.h $(ROOT)/usr/local/include 644 bin bin

clean:
	-rm -f $(OBJECTS) libmikmod.a

clobber: clean
