/* c1fs.h -- core 1's files in memory (c1_syscalls.c) */
#ifndef C1FS_H
#define C1FS_H
#include <stddef.h>
#include <stdint.h>

#define C1FS_MAX    8
#define C1FS_RW     1   /* = C1UA_FILE_RW */

struct c1file {
    char name[128];
    uint8_t *data;
    size_t size;
    int flags;
    uint8_t *dirty;     /* a bit per 64 KB block */
    unsigned ndirty;
};

extern struct c1file c1fs[C1FS_MAX];
int c1fs_add(const char *name, size_t size, int flags);
int c1fs_next_dirty(int *file, size_t *off, size_t *len);
unsigned c1fs_dirty_count(void);

#endif
