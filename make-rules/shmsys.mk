# shmsys.mk -- libshmsys.a for programs that link MIT-SHM statically: the
# shared-memory system calls (shmsys.o, syscall.o) out of the sysroot's
# libc.a, as on AMIX (audio/pt2-clone's files/amix/README.md).  A recipe
# runs $(SHMSYS_BUILD) after it cleans build/ and passes SHM_LIBS on.
SHM_LIBS = $(CURDIR)/build/shm
SHMSYS_BUILD = mkdir -p $(SHM_LIBS) && cd $(SHM_LIBS) && \
	m68k-elf-ar x $(AMIX_SYSROOT)/usr/ccs/lib/libc.a shmsys.o syscall.o && \
	m68k-elf-ar rcsD libshmsys.a shmsys.o syscall.o && rm -f shmsys.o syscall.o
