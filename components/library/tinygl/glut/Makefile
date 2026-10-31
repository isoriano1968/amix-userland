include ../config.mk

# libTinyGLUT.a: the GLUT subset over TinyGL's GLX (see ../include/GL/glut.h)
OBJS = glut.o
LIB = libTinyGLUT.a

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $(LIB)
	$(AR) rcs $(LIB) $(OBJS)
	cp $(LIB) ../lib

clean:
	rm -f *.o *.a

.c.o:
	$(CC) $(CFLAGS) -I../include -c $*.c
