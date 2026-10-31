# LamerCubeAMIX -- an oldskool intro for AMIX (A3000 + Z3660 68060).
# Cross build with gcc14-cross-amix for the 68060, against Mesa 3.1 rebuilt
# with the same compiler (MESA, see mesa31-amix: "make amix060" in Mesa's
# Make-config).  The gcc 2.7 Mesa of the box traps in the 060's FPU: the
# textured cube took 6.3 s per frame there, 30 ms with this one.
#   make            AMIX binary LamerCubeAMIX
#   make host       PC preview build (system GL/GLUT)
#   make assets     regenerate font.h/logo.h (mkassets.py)
MESA = $(HOME)/Sources/usr-amix/mesa31-amix
X    = $(HOME)/opt/m68k-amix-sysroot/usr/x11r6/xc/exports
CC   = $(HOME)/opt/m68k-amix/bin/m68k-elf-gcc
# lib060 (library/lib060): sin/cos/fmod without the instructions the 060 traps on,
# memcpy/memset without AMIX libc's byte loops
M060 = $(HOME)/Sources/usr-amix/amix-userland/components/library/lib060
CFLAGS = -O2 -m68060 -ffast-math -U__linux__ -Ulinux -D__amix__ -DSVR4 \
	-fno-builtin-sin -fno-builtin-cos -fno-builtin-tan -fno-builtin-fmod -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-tanf -fno-builtin-fmodf

# xmu-static: R6.3's libXmu.a only -- the shared libXmu.so names Xt symbols
# but records no dependency; glut needs just the colormap helpers.
# shm-static: the static libXext.a (the shared one has no XShm calls) and
# libc.a's shmsys.o (libc.so.1 does not export shmget/shmat), for Mesa's
# MIT-SHM back buffer (Mesa built with -DSHM): the swap no longer goes
# through the socket.
LamerCubeAMIX: LamerCubeAMIX.c font.h logo.h
	mkdir -p xmu-static && ln -sf $(X)/lib/libXmu.a xmu-static/libXmu.a
	$(CC) $(CFLAGS) -I$(MESA)/include -I$(X)/include -o $@ LamerCubeAMIX.c \
		-L$(MESA)/shm-static -Lxmu-static -L$(MESA)/lib -L$(X)/lib \
		-lglut -lGLU -lGL -lXmu -lXext -lX11 -lshmsys -lsocket -lnsl -L$(M060) -lm060 -lc060 -lm

host: LamerCubeAMIX.c font.h logo.h
	gcc -O2 -Wall -o LamerCubeAMIX-host LamerCubeAMIX.c -lglut -lGLU -lGL -lm

assets:
	python3 -I mkassets.py

# LameCubeAMIGA42: TinyGL 0.4.1 (library/tinygl, built for the 68060 with
# MIT-SHM) instead of Mesa, through library/tinygl's GLUT subset (libTinyGLUT).
TINYGL = $(HOME)/Sources/usr-amix/amix-userland/components/library/tinygl
LameCubeAMIGA42: LameCubeAMIGA42.c font.h logo.h
	$(CC) $(CFLAGS) -I$(TINYGL)/include -I$(X)/include -o $@ LameCubeAMIGA42.c \
		-L$(TINYGL)/lib -lTinyGLUT -lTinyGL -L$(MESA)/shm-static -L$(X)/lib \
		-lXext -lX11 -lshmsys -lsocket -lnsl -L$(M060) -lm060 -lc060 -lm

# LameCubeSUN: LameCubeAMIGA42 with the bouncing Sun logo (sunlogo.h is
# generated from LamerIntroLinux's sun.tga by mksunlogo.py and stays local)
LameCubeSUN: LameCubeSUN.c font.h logo.h sunlogo.h
	$(CC) $(CFLAGS) -I$(TINYGL)/include -I$(X)/include -o $@ LameCubeSUN.c \
		-L$(TINYGL)/lib -lTinyGLUT -lTinyGL -L$(MESA)/shm-static -L$(X)/lib \
		-lXext -lX11 -lshmsys -lsocket -lnsl -L$(M060) -lm060 -lc060 -lm

sunlogo.h: mksunlogo.py
	python3 -I mksunlogo.py

host42: LameCubeAMIGA42.c font.h logo.h
	gcc -O2 -Wall -I$(TINYGL)/include -o LameCubeAMIGA42-host LameCubeAMIGA42.c
