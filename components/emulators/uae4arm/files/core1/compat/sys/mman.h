/* sys/mman.h for core 1: memory is all ours and mapped once (the MMU table
   in core1_main.c); mmap gives aligned heap, mprotect does nothing */
#ifndef C1_SYS_MMAN_H
#define C1_SYS_MMAN_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_NORESERVE 0x4000
#define MAP_FAILED ((void *)-1)
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
#ifdef __cplusplus
}
#endif
#endif
