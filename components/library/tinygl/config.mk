#####################################################################
# C compiler

# linux
CC= $(HOME)/opt/m68k-amix/bin/m68k-elf-gcc
AR= $(HOME)/opt/m68k-amix/bin/m68k-elf-ar
X= $(HOME)/opt/m68k-amix-sysroot/usr/x11r6/xc/exports
# lib060: sin/cos/tan/fmod without the 68881 instructions the 060 emulates,
# and memcpy/memset/... in longs instead of AMIX libc's byte loops
# (78 us per inlined fsin); gcc must call them, not inline them
M060= $(HOME)/Sources/usr-amix/lib060
M060_CFLAGS= -fno-builtin-sin -fno-builtin-cos -fno-builtin-tan -fno-builtin-fmod -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-tanf -fno-builtin-fmodf
CFLAGS= -O2 -m68060 -ffast-math $(M060_CFLAGS) -w -I$(X)/include -D__amix__ -U__linux__ -Ulinux -DM_PI=3.14159265358979323846 -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types
LFLAGS=

# for BeOS PPC
#CC= mwcc
#CFLAGS= -I. -i-
#LFLAGS=

#####################################################################
# TinyGL configuration 

#####################################################################
# Select window API for TinyGL: 

# standard X11 GLX like API 
TINYGL_USE_GLX=y

# BEOS API
#TINYGL_USE_BEOS=y

# Micro Windows NanoX API
#TINYGL_USE_NANOX=y

#####################################################################
# X11 configuration (for the examples only)

ifdef TINYGL_USE_GLX
# Linux
UI_LIBS= -L$(HOME)/Sources/usr-amix/mesa31-amix/shm-static -L$(X)/lib -lXext -lX11 -lshmsys -lsocket -lnsl -L$(M060) -lm060 -lc060
UI_INCLUDES= 
# Solaris
#UI_LIBS= -L/usr/X11/lib -lX11 -lXext -lsocket -lnsl
#UI_INCLUDES= 

UI_OBJS=x11.o
endif

#####################################################################
# Micro windowX11 configuration (for the examples only)

ifdef TINYGL_USE_NANOX
UI_LIBS= -lnano-X -lmwengine -lmwdrivers -lmwfonts
UI_INCLUDES=

# X11 target for nanoX
UI_LIBS+= -L/usr/X11R6/lib -lX11 -lXext

UI_OBJS=nanox.o
endif

#####################################################################
# OpenGL configuration (for the examples only)

# use TinyGL 
GL_LIBS= -L../lib -lTinyGL 
GL_INCLUDES= -I../include
GL_DEPS= ../lib/libTinyGL.a

# use Mesa
#GL_LIBS= -lMesaGL 
#GL_INCLUDES= 
#GL_DEPS=

# use OpenGL
#GL_LIBS= -lGL 
#GL_INCLUDES= 
#GL_DEPS=

####################################################################
# Compile and link control

# UNIX systems
DIRS= src glut examples

# BeOS
# DIRS= src BeOS

